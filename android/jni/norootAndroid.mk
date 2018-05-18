# Android makefile for Open Overlay Router

LOCAL_PATH:= $(call my-dir)
LOCAL_PATH2:= $(LOCAL_PATH)
subdirs := $(addprefix $(LOCAL_PATH)/,$(addsuffix /Android.mk, confuse_android )) 
include $(subdirs)	

LOCAL_PATH:= $(LOCAL_PATH2)/../../oor
include $(CLEAR_VARS)
LOCAL_SRC_FILES = \
		  config/oor_config_confuse.c    \
		  config/oor_config_functions.c  \
   		  control/oor_control.c          \
		  control/oor_ctrl_device.c      \
		  control/oor_local_db.c         \
		  control/oor_map_cache.c        \
		  control/lisp_ms.c              \
		  control/lisp_rtr.c             \
		  control/lisp_tr.c              \
		  control/lisp_xtr.c             \
		  control/control-data-plane/cdp_punt.c    \
		  control/control-data-plane/control-data-plane.c    \
		  control/control-data-plane/vpnapi/cdp_vpnapi.c     \
		  data-plane/data-plane.c        \
		  data-plane/ttable.c            \
		  data-plane/encapsulations/encapsulations.c         \
		  data-plane/encapsulations/vxlan-gpe.c              \
		  data-plane/vpnapi/vpnapi.c     \
		  data-plane/vpnapi/vpnapi_input.c                   \
		  data-plane/vpnapi/vpnapi_output.c                  \
		  elibs/mbedtls/md.c             \
		  elibs/mbedtls/sha1.c           \
		  elibs/mbedtls/sha256.c         \
		  elibs/mbedtls/md_wrap.c        \
		  elibs/patricia/patricia.c      \
		  fwd_policies/balancing_locators.c                  \
          fwd_policies/fwd_addr_func.c   \
          fwd_policies/fwd_policy.c	     \
          fwd_policies/fwd_utils.c	     \
		  fwd_policies/flow_balancing/flow_balancing.c       \
		  fwd_policies/flow_balancing/fwd_entry_tuple.c      \
		  liblisp/liblisp.c              \
		  liblisp/lisp_address.c         \
		  liblisp/lisp_data.c            \
		  liblisp/lisp_ip.c              \
		  liblisp/lisp_lcaf.c            \
		  liblisp/lisp_locator.c         \
		  liblisp/lisp_mapping.c         \
		  liblisp/lisp_messages.c        \
		  liblisp/lisp_message_fields.c  \
		  lib/cksum.c                    \
		  lib/generic_list.c             \
		  lib/hmac.c                     \
		  lib/htable_ptrs.c              \
		  lib/iface_locators.c           \
		  lib/int_table.c                \
		  lib/interfaces_lib.c	         \
		  lib/lbuf.c                     \
		  lib/lisp_site.c                \
		  lib/oor_log.c                  \
		  lib/mapping_db.c               \
		  lib/map_cache_entry.c          \
		  lib/map_cache_rtr_data.c       \
		  lib/map_local_entry.c		     \
		  lib/mem_util.c	    	     \
          lib/nonces_table.c             \
          lib/packets.c                  \
		  lib/prefixes.c                 \
		  lib/routing_tables_lib.c       \
		  lib/sockets.c                  \
		  lib/sockets-util.c             \
		  lib/shash.c                    \
		  lib/timers.c                   \
          lib/timers_utils.c             \
		  lib/util.c                     \
		  net_mgr/net_mgr.c              \
          net_mgr/net_mgr_proc_fc.c      \
          net_mgr/kernel/netm_kernel.c   \
          net_mgr/kernel/iface_mgmt.c    \
		  cmdline.c                      \
		  iface_list.c                   \
		  oor.c                          \
		  oor_jni.c 

LOCAL_CFLAGS += -g -DANDROID -DVPNAPI
LOCAL_C_INCLUDES += $(LOCAL_PATH)/
LOCAL_LDLIBS := -llog
LOCAL_STATIC_LIBRARIES := libconfuse
LOCAL_MODULE = oor

include $(BUILD_SHARED_LIBRARY)


