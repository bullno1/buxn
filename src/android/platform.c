#include "../platform.h"
#include <blog.h>
#include <physfs.h>
#include <sokol_app.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <buxn/vm/vm.h>
#include <buxn/dbg/transports/stream.h>
#include <android/native_activity.h>
#include <android/configuration.h>
#include <android/asset_manager.h>
#include <android/window.h>
#include "../dbg.h"

static struct {
	blog_android_logger_options_t log_options;
	buxn_dbg_integration_t dbg;
	jobject intent_uri;
} platform_android;

typedef struct {
	jobject asset_fd;
	int raw_fd;
} parcel_stream_impl_t;

static int64_t
physfs_read(void* handle, void* buffer, uint64_t num_bytes) {
	return PHYSFS_readBytes(handle, buffer, num_bytes);
}

static int64_t
parcel_read(void* handle, void* buffer, uint64_t num_bytes) {
	parcel_stream_impl_t* impl = handle;
	return read(impl->raw_fd, buffer, num_bytes);
}

void
platform_init(args_t* args) {
	(void)args;  // No op
	memset(&platform_android, 0, sizeof(platform_android));

	ANativeActivity* activity = (ANativeActivity*)sapp_android_get_native_activity();
	ANativeActivity_setWindowFlags(activity, AWINDOW_FLAG_FULLSCREEN, 0);

	jobject ctx = activity->clazz;
	JNIEnv* jenv = activity->env;
	if ((*jenv)->PushLocalFrame(jenv, 16) >= 0) {
		jclass Activity = (*jenv)->GetObjectClass(jenv, ctx);
		jmethodID Activity_getIntent = (*jenv)->GetMethodID(jenv, Activity, "getIntent", "()Landroid/content/Intent;");
		jobject intent = (*jenv)->CallObjectMethod(jenv, ctx, Activity_getIntent);

		jclass Intent = (*jenv)->GetObjectClass(jenv, intent);
		jmethodID Intent_getData = (*jenv)->GetMethodID(jenv, Intent, "getData", "()Landroid/net/Uri;");
		jstring uri = (*jenv)->CallObjectMethod(jenv, intent, Intent_getData);
		if (uri != NULL) {
			platform_android.intent_uri = (*jenv)->NewGlobalRef(jenv, uri);
		}

		(*jenv)->PopLocalFrame(jenv, NULL);
	}
}

sapp_icon_desc
platform_icon(void) {
	return (sapp_icon_desc){ .sokol_default = true };
}

void
platform_cleanup(void) {
	buxn_dbg_integration_cleanup(&platform_android.dbg);
}

void
platform_init_log(void) {
	platform_android.log_options.tag = "buxn";
	blog_add_android_logger(BLOG_LEVEL_DEBUG, &platform_android.log_options);
}

void
platform_init_fs(void) {
	const ANativeActivity* activity = sapp_android_get_native_activity();
	PHYSFS_init((void*)&(struct PHYSFS_AndroidInit){
		.jnienv = activity->env,
		.context = activity->clazz,
	});

	const char* base_dir = PHYSFS_getBaseDir();
	if (!PHYSFS_mount(base_dir, "/", 1)) {
		BLOG_ERROR("Error while mounting apk: %s", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
	}
	PHYSFS_setRoot(base_dir, "/assets");
	// The actual valuee doesn't matter on Android as it will alawys resolve
	// to the data directory.
	const char* write_dir = PHYSFS_getPrefDir("bullno1.com", "buxn");
	PHYSFS_setWriteDir(write_dir);
	PHYSFS_mount(write_dir, "/", 1);
}

void
platform_init_dbg(buxn_vm_t* vm) {
	BLOG_INFO("Trying to connect to debugger");
	int fd = buxn_dbg_transport_abstract_connect("buxn/vm");
	if (fd > 0) {
		BLOG_INFO("Connected to debugger");
		buxn_dbg_integration_init(&platform_android.dbg, vm, fd);
	} else {
		BLOG_INFO("Connection failed: %s", strerror(errno));
	}
}

bool
platform_update_dbg(void) {
	return buxn_dbg_integration_update(&platform_android.dbg);
}

bool
platform_open_boot_rom(stream_t* stream) {
	if (platform_android.intent_uri != NULL) {
		BLOG_DEBUG("Opening rom from intent");

		ANativeActivity* activity = (ANativeActivity*)sapp_android_get_native_activity();
		JavaVM* jvm = activity->vm;
		JNIEnv* jenv;
		if ((*jvm)->AttachCurrentThread(jvm, &jenv, NULL) != JNI_OK) {
			BLOG_ERROR("Could not attach thread to JVM");
			return false;
		}

		stream->read = NULL;

		jclass Activity = (*jenv)->GetObjectClass(jenv, activity->clazz);
		jmethodID Activity_getContentResolver = (*jenv)->GetMethodID(jenv, Activity, "getContentResolver", "()Landroid/content/ContentResolver;");
		jobject resolver = (*jenv)->CallObjectMethod(jenv, activity->clazz, Activity_getContentResolver);

		jclass ContentResolver = (*jenv)->GetObjectClass(jenv, resolver);
		jmethodID ContentResolver_openAssetFileDescriptor = (*jenv)->GetMethodID(jenv, ContentResolver, "openAssetFileDescriptor", "(Landroid/net/Uri;Ljava/lang/String;)Landroid/content/res/AssetFileDescriptor;");
		jstring mode = (*jenv)->NewStringUTF(jenv, "r");
		jobject asset_fd = (*jenv)->CallObjectMethod(jenv, resolver, ContentResolver_openAssetFileDescriptor, platform_android.intent_uri, mode);
		if (!(*jenv)->ExceptionCheck(jenv) || asset_fd == NULL) {
			jclass AssetFileDescriptor = (*jenv)->GetObjectClass(jenv, asset_fd);
			jmethodID AssetFileDescriptor_getParcelFileDescriptor = (*jenv)->GetMethodID(jenv, AssetFileDescriptor, "getParcelFileDescriptor", "()Landroid/os/ParcelFileDescriptor;");
			jobject parcel_fd = (*jenv)->CallObjectMethod(jenv, asset_fd, AssetFileDescriptor_getParcelFileDescriptor);

			jclass ParcelFileDescriptor = (*jenv)->GetObjectClass(jenv, parcel_fd);
			jmethodID ParcelFileDescriptor_getFd = (*jenv)->GetMethodID(jenv, ParcelFileDescriptor, "getFd", "()I");
			int raw_fd = (*jenv)->CallIntMethod(jenv, parcel_fd, ParcelFileDescriptor_getFd);

			parcel_stream_impl_t* impl = malloc(sizeof(parcel_stream_impl_t));
			impl->asset_fd = (*jenv)->NewGlobalRef(jenv, asset_fd);
			impl->raw_fd = raw_fd;
			stream->handle = impl;
			stream->read = parcel_read;
		} else {
			(*jenv)->ExceptionClear(jenv);
			BLOG_ERROR("Exception occured while trying to open rom");
		}

		(*jenv)->DeleteGlobalRef(jenv, platform_android.intent_uri);
		platform_android.intent_uri = NULL;

		(*jvm)->DetachCurrentThread(jvm);

		return stream->read != NULL;
	} else {
		BLOG_DEBUG("Opening default boot rom");

		PHYSFS_File* file = PHYSFS_openRead("boot.rom");
		if (file == NULL) {
			BLOG_ERROR("Could not open rom file: %s", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
			return false;
		}

		stream->handle = file;
		stream->read = physfs_read;
		return true;
	}
}

const char*
platform_stream_error(stream_t stream) {
	(void)stream;
	if (stream.read == physfs_read) {
		return PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode());
	} else if (stream.read == parcel_read) {
		return strerror(errno);
	} else {
		return NULL;
	}
}

void
platform_close_stream(stream_t stream) {
	if (stream.read == physfs_read) {
		PHYSFS_close(stream.handle);
	} else if (stream.read == parcel_read) {
		ANativeActivity* activity = (ANativeActivity*)sapp_android_get_native_activity();
		JavaVM* jvm = activity->vm;
		JNIEnv* jenv;
		if ((*jvm)->AttachCurrentThread(jvm, &jenv, NULL) != JNI_OK) {
			BLOG_ERROR("Could not attach thread to JVM");
			return;
		}

		parcel_stream_impl_t* impl = stream.handle;
		jclass AssetFileDescriptor = (*jenv)->GetObjectClass(jenv, impl->asset_fd);
		jmethodID AssetFileDescriptor_close = (*jenv)->GetMethodID(jenv, AssetFileDescriptor, "close", "()V");
		(*jenv)->CallVoidMethod(jenv, impl->asset_fd, AssetFileDescriptor_close);
		(*jenv)->DeleteGlobalRef(jenv, impl->asset_fd);
		free(impl);

		(*jvm)->DetachCurrentThread(jvm);
	}
}

void
platform_resize_window(uint16_t width, uint16_t height) {
	(void)width;
	(void)height;
}

int
platform_poll_stdin(char* ch, int size) {
	(void)ch;
	(void)size;
	return -1;
}
