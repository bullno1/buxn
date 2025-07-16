#include <buxn/dbg/protocol.h>
#include <buxn/vm/vm.h>

static inline void*
buxn_dbg_protocol_alloc(buxn_dbg_msg_buffer_t buffer, size_t alignment) {
	return (void*)(((intptr_t)buffer + (intptr_t)alignment - 1) & -(intptr_t)alignment);
}

bserial_status_t
buxn_dbg_protocol_msg_header(bserial_ctx_t* ctx, buxn_dbg_msg_t* msg) {
	uint8_t type = msg->type;
	BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &type));
	msg->type = type;
	return BSERIAL_OK;
}

bserial_status_t
buxn_dbg_protocol_msg_body(
	bserial_ctx_t* ctx,
	buxn_dbg_msg_buffer_t buffer,
	buxn_dbg_msg_t* msg
) {
	uint8_t type = msg->type;
	switch ((buxn_dbg_msg_type_t)type) {
		case BUXN_DBG_MSG_BEGIN_EXEC:
			BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &msg->addr));
			break;
		case BUXN_DBG_MSG_BEGIN_BREAK:
			BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &msg->brkp_id));
			break;
		case BUXN_DBG_MSG_COMMAND_REQ:
		case BUXN_DBG_MSG_COMMAND_REP: {
			uint8_t cmd_type = msg->cmd.type;
			BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &cmd_type));
			msg->cmd.type = cmd_type;
			switch (cmd_type) {
				case BUXN_DBG_CMD_INFO:
					if (type == BUXN_DBG_MSG_COMMAND_REQ) {
						uint8_t info_type = msg->cmd.info.type;
						BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &info_type));
						msg->cmd.info.type = info_type;

						if (bserial_mode(ctx) == BSERIAL_MODE_READ) {
							switch (msg->cmd.info.type) {
								case BUXN_DBG_INFO_PC:
									msg->cmd.info.pc = buxn_dbg_protocol_alloc(buffer, _Alignof(uint16_t));
									break;
								case BUXN_DBG_INFO_RST:
								case BUXN_DBG_INFO_WST:
									msg->cmd.info.stack = buxn_dbg_protocol_alloc(buffer, _Alignof(buxn_dbg_stack_info_t));
									break;
								case BUXN_DBG_INFO_NBRKPS:
									msg->cmd.info.nbrkps = buffer;
									break;
							}
						}
					} else {
						switch (msg->cmd.info.type) {
							case BUXN_DBG_INFO_PC:
								BSERIAL_CHECK_STATUS(bserial_any_int(ctx, msg->cmd.info.pc));
								break;
							case BUXN_DBG_INFO_RST:
							case BUXN_DBG_INFO_WST:
								BSERIAL_RECORD(ctx, msg->cmd.info.stack) {
									BSERIAL_KEY(ctx, "pointer") {
										BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &msg->cmd.info.stack->pointer));
									}
									BSERIAL_KEY(ctx, "data") {
										uint64_t len = BUXN_STACK_SIZE;
										BSERIAL_CHECK_STATUS(bserial_blob(ctx, (char*)msg->cmd.info.stack->data, &len));
										if (len != BUXN_STACK_SIZE) { return BSERIAL_MALFORMED; }
									}
								}
								break;
							case BUXN_DBG_INFO_NBRKPS:
								BSERIAL_CHECK_STATUS(bserial_any_int(ctx, msg->cmd.info.nbrkps));
								break;
						}
					}
					break;
				case BUXN_DBG_CMD_BRKP_GET:
						if (type == BUXN_DBG_MSG_COMMAND_REQ) {
							BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &msg->cmd.brkp_get.id));

							if (bserial_mode(ctx) == BSERIAL_MODE_READ) {
								msg->cmd.brkp_get.brkp = buxn_dbg_protocol_alloc(buffer, _Alignof(buxn_dbg_brkp_t));
							}
						} else {
							BSERIAL_RECORD(ctx, &msg->cmd.brkp_get) {
								BSERIAL_KEY(ctx, "addr") {
									BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &msg->cmd.brkp_get.brkp->addr));
								}
								BSERIAL_KEY(ctx, "mask") {
									BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &msg->cmd.brkp_get.brkp->mask));
								}
							}
						}
					break;
				case BUXN_DBG_CMD_BRKP_SET:
					if (type == BUXN_DBG_MSG_COMMAND_REQ) {
						BSERIAL_RECORD(ctx, &msg->cmd.brkp_set) {
							BSERIAL_KEY(ctx, "id") {
								BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &msg->cmd.brkp_set.id));
							}
							BSERIAL_KEY(ctx, "addr") {
								BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &msg->cmd.brkp_set.brkp.addr));
							}
							BSERIAL_KEY(ctx, "mask") {
								BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &msg->cmd.brkp_set.brkp.mask));
							}
						}
					}
					break;
				case BUXN_DBG_CMD_DEV_READ:
					if (type == BUXN_DBG_MSG_COMMAND_REQ) {
						BSERIAL_RECORD(ctx, &msg->cmd.dev_read) {
							BSERIAL_KEY(ctx, "addr") {
								BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &msg->cmd.dev_read.addr));
							}
							BSERIAL_KEY(ctx, "size") {
								BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &msg->cmd.dev_read.size));
							}
						}

						if (bserial_mode(ctx) == BSERIAL_MODE_READ) {
							msg->cmd.dev_read.values = buffer;
						}
					} else {
						uint64_t len = msg->cmd.dev_read.size;
						BSERIAL_CHECK_STATUS(bserial_blob(ctx, (char*)msg->cmd.dev_read.values, &len));
						if (len != (uint64_t)msg->cmd.dev_read.size) {
							return BSERIAL_MALFORMED;
						}
					}
					break;
				case BUXN_DBG_CMD_DEV_WRITE:
					if (type == BUXN_DBG_MSG_COMMAND_REQ) {
						if (bserial_mode(ctx) == BSERIAL_MODE_READ) {
							msg->cmd.dev_write.values = buffer;
						}

						BSERIAL_RECORD(ctx, &msg->cmd.dev_write) {
							BSERIAL_KEY(ctx, "addr") {
								BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &msg->cmd.dev_write.addr));
							}
							BSERIAL_KEY(ctx, "values") {
								uint64_t len = bserial_mode(ctx) == BSERIAL_MODE_READ
									? BUXN_DEVICE_MEM_SIZE
									: msg->cmd.dev_write.size;
								BSERIAL_CHECK_STATUS(bserial_blob(ctx, (char*)msg->cmd.dev_write.values, &len));
								msg->cmd.dev_write.size = (uint8_t)len;
							}
						}
					}
					break;
				case BUXN_DBG_CMD_MEM_READ:
					if (type == BUXN_DBG_MSG_COMMAND_REQ) {
						BSERIAL_RECORD(ctx, &msg->cmd.mem_read) {
							BSERIAL_KEY(ctx, "addr") {
								BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &msg->cmd.mem_read.addr));
							}
							BSERIAL_KEY(ctx, "size") {
								BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &msg->cmd.mem_read.size));
								if (msg->cmd.mem_read.size > BUXN_DBG_MAX_MEM_ACCESS_SIZE) {
									return BSERIAL_MALFORMED;
								}
							}
						}

						if (bserial_mode(ctx) == BSERIAL_MODE_READ) {
							msg->cmd.mem_read.values = buffer;
						}
					} else {
						uint64_t len = msg->cmd.mem_read.size;
						BSERIAL_CHECK_STATUS(bserial_blob(ctx, (char*)msg->cmd.mem_read.values, &len));
						if (len != (uint64_t)msg->cmd.mem_read.size) {
							return BSERIAL_MALFORMED;
						}
					}
					break;
				case BUXN_DBG_CMD_MEM_WRITE:
					if (type == BUXN_DBG_MSG_COMMAND_REQ) {
						if (bserial_mode(ctx) == BSERIAL_MODE_READ) {
							msg->cmd.mem_write.values = buffer;
						}

						BSERIAL_RECORD(ctx, &msg->cmd.mem_write) {
							BSERIAL_KEY(ctx, "addr") {
								BSERIAL_CHECK_STATUS(bserial_any_int(ctx, &msg->cmd.mem_write.addr));
							}
							BSERIAL_KEY(ctx, "values") {
								uint64_t len = bserial_mode(ctx) == BSERIAL_MODE_READ
									? BUXN_DBG_MAX_MEM_ACCESS_SIZE
									: msg->cmd.mem_write.size;
								BSERIAL_CHECK_STATUS(bserial_blob(ctx, (char*)msg->cmd.mem_write.values, &len));
								msg->cmd.mem_write.size = (uint8_t)len;
							}
						}
					}
					break;
				case BUXN_DBG_CMD_RESUME:
				case BUXN_DBG_CMD_STEP_IN:
				case BUXN_DBG_CMD_STEP_OUT:
				case BUXN_DBG_CMD_STEP_OVER:
					break;
			}
		} break;
		case BUXN_DBG_MSG_END_EXEC:
		case BUXN_DBG_MSG_END_BREAK:
		case BUXN_DBG_MSG_PAUSED:
			break;
		default:
			return BSERIAL_MALFORMED;
	}

	return BSERIAL_OK;
}
