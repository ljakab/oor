/*
 *
 * Copyright (C) 2011, 2015 Cisco Systems, Inc.
 * Copyright (C) 2015 CBA research group, Technical University of Catalonia.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "lisp_tr.h"
#include "../lib/iface_locators.h"
#include "../lib/nonces_table.h"
#include "../lib/oor_log.h"
#include "../lib/prefixes.h"
#include "../lib/timers_utils.h"
#include "../oor_external.h"


/************************** Function declaration *****************************/

static void *tr_get_device(lisp_tr_t *tr);
static oor_ctrl_dev_t * tr_get_ctrl_device(lisp_tr_t *tr);

/*****************************************************************************/

int
lisp_tr_init(lisp_tr_t *tr)
{

    tr->map_cache = mcache_new();
    tr->map_resolvers = glist_new_managed((glist_del_fct)lisp_addr_del);
    tr->iface_locators_table = shash_new_managed((free_value_fn_t)iface_locators_del);
    /* fwd_policy and fwd_policy_dev_parm are initialized during configuration process */
    if (!tr->map_cache || !tr->map_resolvers || !tr->iface_locators_table){
        return (BAD);
    }
    return (GOOD);
}

void
lisp_tr_uninit(lisp_tr_t *tr)
{
    if (!tr){
        return;
    }

    shash_destroy(tr->iface_locators_table);
    mcache_del(tr->map_cache);
    glist_destroy(tr->map_resolvers);
    if (tr->fwd_policy_dev_parm){
        tr->fwd_policy->del_dev_policy_inf(tr->fwd_policy_dev_parm);
    }
}

inline tr_abstract_device *
lisp_tr_abstract_cast(oor_ctrl_dev_t *ctrl_dev)
{
    return(CONTAINER_OF(ctrl_dev,tr_abstract_device,super));
}

static void *
tr_get_device(lisp_tr_t *tunnel_router)
{
    return (CONTAINER_OF(tunnel_router,tr_abstract_device,tr));
}

/* Return the ctrl device structure associated with the tr */
static oor_ctrl_dev_t *
tr_get_ctrl_device(lisp_tr_t *tunnel_router)
{
    tr_abstract_device *tr_ctrl = CONTAINER_OF(tunnel_router,tr_abstract_device,tr);
    return (&tr_ctrl->super);
}




/*************************** PROCESS MESSAGES ********************************/

int
tr_recv_map_reply(lisp_tr_t *tr, lbuf_t *buf, uconn_t *udp_con)
{
    void *mrep_hdr;
    locator_t *probed;
    lisp_addr_t *probed_addr;
    lbuf_t b;
    mcache_entry_t *mce;
    mapping_t *m;
    nonces_list_t *nonces_lst;
    oor_timer_t *timer;
    timer_map_req_argument *t_mr_arg;
    int records,active_entry,i;

    /* local copy */
    b = *buf;

    mrep_hdr = lisp_msg_pull_hdr(&b);

    /* Check NONCE */
    nonces_lst = htable_nonces_lookup(nonces_ht, MREP_NONCE(mrep_hdr));
    if (!nonces_lst){
        OOR_LOG(LDBG_2, " Nonce %"PRIx64" doesn't match any Map-Request nonce. "
                "Discarding message!", htonll(MREP_NONCE(mrep_hdr)));
        return(BAD);
    }
    timer = nonces_list_timer(nonces_lst);
    /* If it is not a Map Reply Probe */
    if (!MREP_RLOC_PROBE(mrep_hdr)){
        t_mr_arg = (timer_map_req_argument *)oor_timer_cb_argument(timer);
        /* We only accept one record except when the nonce is generated by a not active entry */
        mce = t_mr_arg->mce;

        active_entry = mcache_entry_active(mce);
        if (!active_entry){
            records = MREP_REC_COUNT(mrep_hdr);
            /* delete placeholder/dummy mapping inorder to install the new one */
            tr_mcache_remove_entry(tr, mce);
            /* Timers are removed during the process of deleting the mce*/
            timer = NULL;
        }else{
            if (MREP_REC_COUNT(mrep_hdr) >1){
                OOR_LOG(LINF,"Received Map Reply with multiple records. Only first one will be processed");
            }
            records = 1;
        }

        for (i = 0; i < records; i++) {
            m = mapping_new();
            if (lisp_msg_parse_mapping_record(&b, m, &probed) != GOOD) {
                goto err;
            }
            if (mapping_has_elp_with_l_bit(m)){
                OOR_LOG(LDBG_1,"Received a Map Reply with an ELP with the L bit set. "
                        "Not supported -> Discrding map reply");
                goto err;
            }

            /* Mapping is NOT ACTIVE */
            if (!active_entry) {
                /* Check we don't have already an entry for the mapping */
                mce = mcache_lookup_exact(tr->map_cache, mapping_eid(m));
                if (mce){
                    OOR_LOG(LDBG_2,"Received a Map Reply for a recently activated cache entry. Ignoring the msg");
                    mapping_del(m);
                    continue;
                }
                /* DO NOT free mapping in this case */
                mce = tr_mcache_add_mapping(tr, m, MCE_DYNAMIC, ACTIVE);
                if (mce){
                    tr_mcache_entry_program_timers(tr,mce);
                    OOR_LOG(LDBG_1, "Added Map Cache entry with EID prefix %s in the database.",
                            lisp_addr_to_char(mapping_eid(m)));
                }else{
                    OOR_LOG(LERR, "Can't add Map Cache entry with EID prefix %s. Discarded ...",
                            mapping_eid(m));
                    mapping_del(m);
                    continue;
                }
                /* Mapping is ACTIVE */
            } else {
                /* the reply might be for an active mapping (SMR)*/
                if (tr_update_mcache_entry(tr, m) == GOOD){
                    mce = mcache_lookup_exact(tr->map_cache, mapping_eid(m));
                    tr_mcache_entry_program_timers(tr, mce);
                    /* Update data plane */
                    notify_datap_rm_fwd_from_entry(tr_get_ctrl_device(tr),mapping_eid(m),FALSE);
                    mapping_del(m);
                }
            }

            mcache_dump_db(tr->map_cache, LDBG_3);
        }
    }else{
        if (MREP_REC_COUNT(mrep_hdr) >1){
            OOR_LOG(LDBG_1,"Received Map Reply Probe with multiple records. Only first one will be processed");
        }
        records = 1;
        for (i = 0; i < records; i++) {
            m = mapping_new();
            if (lisp_msg_parse_mapping_record(&b, m, &probed) != GOOD) {
                goto err;
            }
            if (mapping_has_elp_with_l_bit(m)){
                OOR_LOG(LDBG_1,"Received a Map Reply with an ELP with the L bit set. "
                        "Not supported -> Discrding map reply");
                goto err;
            }

            if (probed != NULL){
                probed_addr = locator_addr(probed);
            }else{
                probed_addr = &(udp_con->ra);
            }

            mce = mcache_lookup_exact(tr->map_cache, mapping_eid(m));
            if (!mce){
                OOR_LOG(LDBG_2,"Received a non requested Map Reply probe");
                return (BAD);
            }

            handle_locator_probe_reply(tr, mce, probed_addr);

            /* No need to free 'probed' since it's a pointer to a locator in
             * of m's */
            mapping_del(m);
        }
    }
    if (timer != NULL){
        /* Remove nonces_lst and associated timer*/
        stop_timer_from_obj(mce,timer,ptrs_to_timers_ht,nonces_ht);
    }

    return(GOOD);
err:
    locator_del(probed);
    mapping_del(m);
    return(BAD);
}


int
tr_reply_to_smr(lisp_tr_t *tr, lisp_addr_t *src_eid, lisp_addr_t *req_eid)
{
    mcache_entry_t *mce;
    oor_timer_t *timer;
    timer_map_req_argument *timer_arg;

    /* Lookup the map cache entry that match with the requested EID prefix */
    if (!(mce = mcache_lookup(tr->map_cache, req_eid))) {
        OOR_LOG(LDBG_2,"tr_reply_to_smr: Received a solicited SMR from %s but it "
                "doesn't exist in cache", lisp_addr_to_char(req_eid));
        return(BAD);
    }

    /* Creat timer responsible of retries */
    timer_arg = timer_map_req_arg_new_init(mce,src_eid);
    timer = oor_timer_with_nonce_new(SMR_INV_RETRY_TIMER, tr_get_device(tr), tr_smr_invoked_map_request_cb,
            timer_arg,(oor_timer_del_cb_arg_fn)timer_map_req_arg_free);

    htable_ptrs_timers_add(ptrs_to_timers_ht, mce, timer);

    tr_smr_invoked_map_request_cb(timer);

    return(GOOD);
}


static int
tr_send_smr_invoked_map_request(lisp_tr_t *tr, lisp_addr_t *src_eid,
        mcache_entry_t *mce, uint64_t nonce)
{
    uconn_t uc;
    lisp_addr_t *drloc, *srloc;
    struct lbuf *b = NULL;
    void *hdr = NULL;
    mapping_t *m = NULL;
    lisp_addr_t *deid = NULL, *s_in_addr = NULL, *d_in_addr = NULL;
    glist_t *rlocs = NULL;
    int afi, ret ;

    m = mcache_entry_mapping(mce);
    deid = mapping_eid(m);
    afi = lisp_addr_ip_afi(deid);

    OOR_LOG(LDBG_1,"SMR: Map-Request for EID: %s",lisp_addr_to_char(deid));

    /* BUILD Map-Request */

    /* no source EID and mapping, so put default control rlocs */
    rlocs = ctrl_default_rlocs(ctrl_dev_get_ctrl_t(tr_get_ctrl_device(tr)));
    b = lisp_msg_mreq_create(src_eid, rlocs, mapping_eid(m));
    if (b == NULL){
        OOR_LOG(LWRN, "send_smr_invoked_map_request: Couldn't create map request message");
        glist_destroy(rlocs);
        return (BAD);
    }

    hdr = lisp_msg_hdr(b);
    MREQ_SMR_INVOKED(hdr) = 1;
    MREQ_NONCE(hdr) = nonce;

    /* we could put anything here. Still, better put something that
     * makes a bit of sense .. */
    d_in_addr = pref_get_network_address(lisp_addr_get_ip_pref_addr(deid));
    if (lisp_addr_ip_afi (src_eid) == afi){
        s_in_addr = lisp_addr_clone(lisp_addr_get_ip_addr(src_eid));
    }else{
        s_in_addr = ctrl_default_rloc(ctrl_dev_get_ctrl_t(tr_get_ctrl_device(tr)),afi);
        if (s_in_addr == NULL){
            OOR_LOG(LDBG_1,"SMR: Couldn't generate Map-Request for EID: %s. No source inner ip address available)",
                    lisp_addr_to_char(deid));
            lisp_addr_del(d_in_addr);
            return (BAD);
        }
        s_in_addr = lisp_addr_clone(s_in_addr);
    }

    /* SEND */
    OOR_LOG(LDBG_1, "%s, itr-rlocs:%s src-eid: %s, req-eid: %s",
            lisp_msg_hdr_to_char(b), laddr_list_to_char(rlocs),
            lisp_addr_to_char(src_eid), lisp_addr_to_char(deid));

    /* Encapsulate messgae and send it to the map resolver */
    lisp_msg_encap(b, LISP_CONTROL_PORT, LISP_CONTROL_PORT, s_in_addr,
            d_in_addr);

    srloc = NULL;
    drloc = get_map_resolver(tr);
    if (!drloc){
        glist_destroy(rlocs);
        lisp_addr_del(s_in_addr);
        lisp_addr_del(d_in_addr);
        lisp_msg_destroy(b);
        return (BAD);
    }

    uconn_init(&uc, LISP_CONTROL_PORT, LISP_CONTROL_PORT, srloc, drloc);
    ret = send_msg(tr_get_ctrl_device(tr), b, &uc);

    glist_destroy(rlocs);
    lisp_msg_destroy(b);
    lisp_addr_del(s_in_addr);
    lisp_addr_del(d_in_addr);

    return (ret);
}


/* Sends Encap Map-Request for EID in 'mce' and sets-up a retry timer */
int
tr_build_and_send_encap_map_request(lisp_tr_t *tr, lisp_addr_t *seid,
        mcache_entry_t *mce, uint64_t nonce)
{
    uconn_t uc;
    mapping_t *m = NULL;
    lisp_addr_t *deid = NULL;
    lisp_addr_t *drloc, *srloc;
    glist_t *rlocs = NULL;
    lbuf_t *b = NULL;
    void *mr_hdr = NULL;

    if (glist_size(tr->map_resolvers) == 0){
        OOR_LOG(LDBG_1, "Couldn't send encap map request: No map resolver configured");
        return (BAD);
    }

    m = mcache_entry_mapping(mce);
    deid = mapping_eid(m);

    /* BUILD Map-Request */

    // Rlocs to be used as ITR of the map req.
    rlocs = ctrl_default_rlocs(ctrl_dev_get_ctrl_t(tr_get_ctrl_device(tr)));
    OOR_LOG(LDBG_1, "locators for req: %s", laddr_list_to_char(rlocs));
    b = lisp_msg_mreq_create(seid, rlocs, deid);
    if (b == NULL) {
        OOR_LOG(LDBG_1, "tr_build_and_send_encap_map_request: Couldn't create map request message");
        glist_destroy(rlocs);
        return(BAD);
    }

    mr_hdr = lisp_msg_hdr(b);
    MREQ_NONCE(mr_hdr) = nonce;
    OOR_LOG(LDBG_1, "%s, itr-rlocs:%s, src-eid: %s, req-eid: %s",
            lisp_msg_hdr_to_char(b), laddr_list_to_char(rlocs),
            lisp_addr_to_char(seid), lisp_addr_to_char(deid));
    glist_destroy(rlocs);


    /* Encapsulate message and send it to the map resolver */

    lisp_msg_encap(b, LISP_CONTROL_PORT, LISP_CONTROL_PORT, seid, deid);

    srloc = NULL;
    drloc = get_map_resolver(tr);
    if (!drloc){
        lisp_msg_destroy(b);
        return (BAD);
    }

    uconn_init(&uc, LISP_CONTROL_PORT, LISP_CONTROL_PORT, srloc, drloc);
    send_msg(tr_get_ctrl_device(tr), b, &uc);
    lisp_msg_destroy(b);

    return(GOOD);
}

/* Send a Map-Request probe to check status of 'loc' */
int
tr_build_and_send_mreq_probe(lisp_tr_t *tr, mapping_t *map, locator_t *loc, uint64_t nonce)
{
    uconn_t uc;
    lisp_addr_t * deid, *drloc, empty;
    lbuf_t * b;
    glist_t * rlocs = NULL;
    void * hdr = NULL;
    int ret;
    oor_ctrl_t *ctrl;

    deid = mapping_eid(map);

    ctrl = ctrl_dev_get_ctrl_t(tr_get_ctrl_device(tr));
    // XXX alopez -> What we have to do with ELP and probe bit
    drloc = tr->fwd_policy->get_fwd_ip_addr(locator_addr(loc), ctrl_rlocs(ctrl));
    lisp_addr_set_lafi(&empty, LM_AFI_NO_ADDR);

    rlocs = ctrl_default_rlocs(ctrl);
    b = lisp_msg_mreq_create(&empty, rlocs, deid);
    glist_destroy(rlocs);
    if (b == NULL){
        return (BAD);
    }

    hdr = lisp_msg_hdr(b);
    MREQ_NONCE(hdr) = nonce;
    MREQ_RLOC_PROBE(hdr) = 1;

    uconn_init(&uc, LISP_CONTROL_PORT, LISP_CONTROL_PORT, NULL, drloc);
    ret = send_msg(tr_get_ctrl_device(tr), b, &uc);
    lisp_msg_destroy(b);

    return (ret);
}

/**************************** LOGICAL PROCESSES ******************************/
/************************** Map Cache Expiration *****************************/

/* Called when the timer associated with an EID entry expires. */
int
tr_mc_entry_expiration_timer_cb(oor_timer_t *timer)
{
    mcache_entry_t *mce = oor_timer_cb_argument(timer);
    mapping_t *map = mcache_entry_mapping(mce);
    lisp_addr_t *addr = mapping_eid(map);
    tr_abstract_device *tr_dev = oor_timer_owner(timer);

    OOR_LOG(LDBG_1,"Got expiration for EID %s", lisp_addr_to_char(addr));
    tr_mcache_remove_entry(&tr_dev->tr, mce);
    return(GOOD);
}

void
tr_mc_entry_program_expiration_timer(lisp_tr_t *tr, mcache_entry_t *mce)
{
    stop_timers_of_type_from_obj(mce,EXPIRE_MAP_CACHE_TIMER,ptrs_to_timers_ht, nonces_ht);
    int time = mapping_ttl(mcache_entry_mapping(mce))*60;
    tr_mc_entry_program_expiration_timer2(tr, mce, time);
}

void
tr_mc_entry_program_expiration_timer2(lisp_tr_t *tr, mcache_entry_t *mce, int time)
{
    /* Expiration cache timer */
    oor_timer_t *timer;

    timer = oor_timer_without_nonce_new(EXPIRE_MAP_CACHE_TIMER,tr_get_device(tr),tr_mc_entry_expiration_timer_cb,mce,NULL);
    htable_ptrs_timers_add(ptrs_to_timers_ht, mce, timer);

    oor_timer_start(timer, time);

    if (time > 60){
        OOR_LOG(LDBG_1,"The map cache entry of EID %s will expire in %d minutes",
                lisp_addr_to_char(mapping_eid(mcache_entry_mapping(mce))),time/60);
    }else{
        OOR_LOG(LDBG_1,"The map cache entry of EID %s will expire in %d seconds",
                lisp_addr_to_char(mapping_eid(mcache_entry_mapping(mce))),time);
    }
}

/**************************** SMR invoked timer  *****************************/

int
tr_smr_invoked_map_request_cb(oor_timer_t *timer)
{
    timer_map_req_argument *timer_arg = (timer_map_req_argument *)oor_timer_cb_argument(timer);
    nonces_list_t *nonces_list = oor_timer_nonces(timer);
    tr_abstract_device *tr_dev = oor_timer_owner(timer);
    uint64_t nonce;
    lisp_addr_t *deid;

    if (nonces_list_size(nonces_list) - 1 < OOR_MAX_SMR_RETRANSMIT) {
        nonce = nonce_new();
        if (tr_send_smr_invoked_map_request(&tr_dev->tr, timer_arg->src_eid, timer_arg->mce, nonce) != GOOD){
            return (BAD);
        }
        htable_nonces_insert(nonces_ht, nonce, nonces_list);
        oor_timer_start(timer, OOR_INITIAL_SMR_TIMEOUT);
        return (GOOD);
    } else {
        deid = mapping_eid(mcache_entry_mapping(timer_arg->mce));
        OOR_LOG(LDBG_1,"SMR: No Map Reply for EID %s. Removing entry ...",
                lisp_addr_to_char(deid));
        tr_mcache_remove_entry(&tr_dev->tr,timer_arg->mce);
        return (BAD);
    }
}

/***************************** RLOC Probing **********************************/


/* Program RLOC probing for each locator of the mapping */
void
tr_program_mce_rloc_probing(lisp_tr_t *tr, mcache_entry_t *mce)
{
    mapping_t *map;
    locator_t *locator;

    if (tr->probe_interval == 0) {
        return;
    }
    /* Cancel previous RLOCs Probing associated to this mce */
    stop_timers_of_type_from_obj(mce,RLOC_PROBING_TIMER,ptrs_to_timers_ht, nonces_ht);

    map = mcache_entry_mapping(mce);
    /* Start rloc probing for each locator of the mapping */
    mapping_foreach_active_locator(map,locator){
        // XXX alopez: Check if RLOC probing is available for all LCAF. ELP RLOC Probing bit
        tr_program_rloc_probing(tr, mce, locator, tr->probe_interval);
    }mapping_foreach_active_locator_end;
}



void
tr_program_rloc_probing(lisp_tr_t *tr, mcache_entry_t *mce, locator_t *loc, int time)
{
    oor_timer_t *timer;
    timer_rloc_probe_argument *arg;

    if (locator_R_bit(loc) == 0 || locator_priority(loc) == UNUSED_RLOC_PRIORITY){
        return;
    }

    arg = timer_rloc_probe_argument_new_init(mce,loc);
    timer = oor_timer_with_nonce_new(RLOC_PROBING_TIMER,tr_get_device(tr),tr_rloc_probing_cb,
            arg,(oor_timer_del_cb_arg_fn)timer_rloc_probe_argument_free);
    htable_ptrs_timers_add(ptrs_to_timers_ht, mce, timer);

    oor_timer_start(timer, time);
    OOR_LOG(LDBG_2,"Programming probing of EID's %s locator %s (%d seconds)",
            lisp_addr_to_char(mapping_eid(mcache_entry_mapping(mce))),
            lisp_addr_to_char(locator_addr(loc)), time);

}


int
tr_rloc_probing_cb(oor_timer_t *timer)
{
    timer_rloc_probe_argument *rparg = oor_timer_cb_argument(timer);
    nonces_list_t *nonces_lst = oor_timer_nonces(timer);
    tr_abstract_device *tr_dev = oor_timer_owner(timer);
    lisp_tr_t *tr = &tr_dev->tr;
    mapping_t *map = mcache_entry_mapping(rparg->mce);
    locator_t *loct = rparg->locator;
    lisp_addr_t * drloc;
    uint64_t nonce;
    mcache_entry_t * mce;

    // XXX alopez -> What we have to do with ELP and probe bit
    drloc = tr->fwd_policy->get_fwd_ip_addr(locator_addr(loct), ctrl_rlocs(ctrl_dev_get_ctrl_t(tr_get_ctrl_device(tr))));

    if ((nonces_list_size(nonces_lst) -1) < tr->probe_retries){
        nonce = nonce_new();
        if (tr_build_and_send_mreq_probe(tr, map,loct,nonce) != GOOD){
            /* Retry send RLOC Probe in rloc probe interval. No short retries */
            goto no_probe;
        }
        if (nonces_list_size(nonces_lst) > 0) {
            OOR_LOG(LDBG_1,"Retry Map-Request Probe for locator %s and "
                    "EID: %s (%d retries)", lisp_addr_to_char(drloc),
                    lisp_addr_to_char(mapping_eid(map)), nonces_list_size(nonces_lst));
        } else {
            OOR_LOG(LDBG_1,"Map-Request Probe for locator %s and "
                    "EID: %s", lisp_addr_to_char(drloc),
                    lisp_addr_to_char(mapping_eid(map)));
        }
        htable_nonces_insert(nonces_ht, nonce,nonces_lst);
        oor_timer_start(timer, tr->probe_retries_interval);
        return (GOOD);
    }else{
no_probe:
        /* If we have reached maximum number of retransmissions, change remote
         *  locator status */
        if (locator_state(loct) == UP) {
            locator_set_state(loct, DOWN);
            OOR_LOG(LDBG_1,"rloc_probing: No Map-Reply Probe received for locator"
                    " %s and EID: %s -> Locator state changes to DOWN",
                    lisp_addr_to_char(drloc), lisp_addr_to_char(mapping_eid(map)));

            /* [re]Calculate forwarding info  if it has been a change
             * of status*/
            mce = mcache_lookup_exact(tr->map_cache,mapping_eid(map));
            if (mce == NULL){
                OOR_LOG(LERR,"rloc_probing: No map cache entry for EID %s. It should never happened",
                        lisp_addr_to_char(mapping_eid(map)));
                return(BAD);
            }

            tr->fwd_policy->updated_map_cache_inf(tr->fwd_policy_dev_parm,mce);
            notify_datap_rm_fwd_from_entry(tr_get_ctrl_device(tr),mcache_entry_eid(mce),FALSE);
        }

        /* Reprogram time for next probe interval */
        htable_nonces_reset_nonces_lst(nonces_ht,nonces_lst);
        oor_timer_start(timer, tr->probe_interval);
        OOR_LOG(LDBG_2,"Reprogramed RLOC probing of the locator %s of the EID %s "
                "in %d seconds", lisp_addr_to_char(drloc),
                lisp_addr_to_char(mapping_eid(map)), tr->probe_interval);

        return (BAD);
    }
}

/* Process a record from map-reply probe message */
int
handle_locator_probe_reply(lisp_tr_t *tr, mcache_entry_t *mce,
        lisp_addr_t *probed_addr)
{
    locator_t * loct = NULL;
    mapping_t * map = NULL;

    map = mcache_entry_mapping(mce);
    loct = mapping_get_loct_with_addr(map, probed_addr);


    if (!loct){
        OOR_LOG(LDBG_2,"Probed locator %s not part of the the mapping %s",
                lisp_addr_to_char(probed_addr),
                lisp_addr_to_char(mapping_eid(map)));
        return (ERR_NO_EXIST);
    }


    OOR_LOG(LDBG_1," Successfully probed RLOC %s of cache entry with EID %s",
                lisp_addr_to_char(locator_addr(loct)),
                lisp_addr_to_char(mapping_eid(map)));


    if (loct->state == DOWN) {
        loct->state = UP;

        OOR_LOG(LDBG_1," Locator %s state changed to UP",
                lisp_addr_to_char(locator_addr(loct)));

        /* [re]Calculate forwarding info if status changed*/
        tr->fwd_policy->updated_map_cache_inf(tr->fwd_policy_dev_parm,mce);
        notify_datap_rm_fwd_from_entry(tr_get_ctrl_device(tr),mapping_eid(map),FALSE);
    }

    /* Reprogramming timers of rloc probing */
    tr_program_rloc_probing(tr, mce, loct, tr->probe_interval);

    return (GOOD);
}

/*************************** Map Cache miss **********************************/

int
handle_map_cache_miss(lisp_tr_t *tr, lisp_addr_t *requested_eid,
        lisp_addr_t *src_eid)
{
    mcache_entry_t *mce;
    mapping_t *m;
    oor_timer_t *timer;
    timer_map_req_argument *timer_arg;
    int ret;

    /* Install temporary, NOT active, mapping in map_cache */
    m = mapping_new_init(requested_eid);
    mapping_set_action(m, ACT_NATIVE_FWD);
    mce = tr_mcache_add_mapping(tr,m, MCE_DYNAMIC, NOT_ACTIVE);
    if (!mce){
        OOR_LOG(LWRN, "Couln't install temporary map cache entry for %s!",
                lisp_addr_to_char(requested_eid));
        mcache_entry_del(mce);
        return (BAD);
    }
    printf ("ADD not active map cache entry for EID %s\n", lisp_addr_to_char(requested_eid));

    timer_arg = timer_map_req_arg_new_init(mce,src_eid);
    timer = oor_timer_with_nonce_new(MAP_REQUEST_RETRY_TIMER,tr_get_device(tr),send_map_request_retry_cb,
            timer_arg,(oor_timer_del_cb_arg_fn)timer_map_req_arg_free);
    htable_ptrs_timers_add(ptrs_to_timers_ht,mce,timer);

    ret = send_map_request_retry_cb(timer);
    if (ret == BAD){
        // We mantain the entry in the cache with a small expiration time
        // During this expiration time the data plane will not interrupt the control plane
        tr_mc_entry_program_expiration_timer2(tr, mce, 10);
    }

    return(ret);
}

int
send_map_request_retry_cb(oor_timer_t *timer)
{
    timer_map_req_argument *timer_arg = (timer_map_req_argument *)oor_timer_cb_argument(timer);
    nonces_list_t *nonces_list = oor_timer_nonces(timer);
    tr_abstract_device *tr_dev = oor_timer_owner(timer);
    lisp_tr_t *tr = &tr_dev->tr;
    uint64_t nonce;
    lisp_addr_t *deid;
    int retries = nonces_list_size(nonces_list);

    deid = mapping_eid (mcache_entry_mapping(timer_arg->mce));
    if (retries - 1 < tr->map_request_retries) {

        if (retries > 0) {
            OOR_LOG(LDBG_1, "Retransmitting Map Request for EID: %s (%d retries)",
                    lisp_addr_to_char(deid), retries);
        }
        nonce = nonce_new();
        if (tr_build_and_send_encap_map_request(tr, timer_arg->src_eid, timer_arg->mce, nonce) != GOOD){
            return (BAD);
        }
        htable_nonces_insert(nonces_ht, nonce, nonces_list);
        oor_timer_start(timer, OOR_INITIAL_MRQ_TIMEOUT);
        return (GOOD);
    } else {
        OOR_LOG(LDBG_1, "No Map-Reply for EID %s after %d retries. Aborting!",
                lisp_addr_to_char(deid), retries -1 );
        /* When removing mce, all timers associated to it are canceled */
        tr_mcache_remove_entry(tr,timer_arg->mce);

        return (ERR_NO_REPLY);
    }
}

/******************************* TIMERS **************************************/
/*********************** Map Cache Expiration timer  *************************/


timer_rloc_probe_argument *
timer_rloc_probe_argument_new_init(mcache_entry_t *mce,locator_t *locator)
{
    timer_rloc_probe_argument *timer_arg = xmalloc(sizeof(timer_rloc_probe_argument));
    timer_arg->mce = mce;
    timer_arg->locator = locator;
    return (timer_arg);
}

void
timer_rloc_probe_argument_free(timer_rloc_probe_argument *timer_arg){
    free (timer_arg);
}

timer_map_req_argument *
timer_map_req_arg_new_init(mcache_entry_t *mce,lisp_addr_t *src_eid)
{
    timer_map_req_argument *timer_arg = xmalloc(sizeof(timer_map_req_argument));
    timer_arg->mce = mce;
    timer_arg->src_eid = lisp_addr_clone(src_eid);

    return(timer_arg);
}

void
timer_map_req_arg_free(timer_map_req_argument * timer_arg)
{
    lisp_addr_del(timer_arg->src_eid);
    free(timer_arg);
}

/***************************  Map cache functions ****************************/

mcache_entry_t *
tr_mcache_add_mapping(lisp_tr_t *tr, mapping_t *m, mce_type_e how_learned, uint8_t is_active)
{
    mcache_entry_t *mce;

    mce = mcache_entry_new();
    if (mce == NULL){
        return (NULL);
    }

    if (how_learned == MCE_DYNAMIC){
        mcache_entry_init(mce, m);
    }else{
        mcache_entry_init_static(mce, m);
    }

    /* Precalculate routing information */
    if (tr->fwd_policy->init_map_cache_policy_inf(tr->fwd_policy_dev_parm,mce) != GOOD){
        OOR_LOG(LWRN, "tr_mcache_add_mapping: Couldn't initiate routing info for map cache entry %s!. Discarding it.",
                lisp_addr_to_char(mapping_eid(m)));
        mcache_entry_del(mce);
        return(NULL);
    }

    if (mcache_add_entry(tr->map_cache, mapping_eid(m), mce) != GOOD) {
        OOR_LOG(LDBG_1, "tr_mcache_add_mapping: Couldn't add map cache entry %s to data base!. Discarding it.",
                lisp_addr_to_char(mapping_eid(m)));
        mcache_entry_del(mce);
        return(NULL);
    }

    if (is_active){
        mcache_entry_set_active(mce, ACTIVE);
    }else{
        mcache_entry_set_active(mce, NOT_ACTIVE);
    }

    return(mce);
}

/* Remove an entry from the cache and destroy it */
int
tr_mcache_remove_entry(lisp_tr_t *tr, mcache_entry_t *mce)
{
    void *data = NULL;
    lisp_addr_t *eid = mapping_eid(mcache_entry_mapping(mce));

    notify_datap_rm_fwd_from_entry(tr_get_ctrl_device(tr),eid,FALSE);

    data = mcache_remove_entry(tr->map_cache, eid);
    mcache_entry_del(data);
    mcache_dump_db(tr->map_cache, LDBG_3);

    return (GOOD);
}


int
tr_update_mcache_entry(lisp_tr_t *tr, mapping_t *recv_map)
{
    mcache_entry_t *mce = NULL;
    mapping_t *map = NULL;
    lisp_addr_t *eid = NULL;


    eid = mapping_eid(recv_map);

    /* Serch map cache entry exist*/
    mce = mcache_lookup_exact(tr->map_cache, eid);
    if (!mce){
        OOR_LOG(LDBG_2,"No map cache entry for %s", lisp_addr_to_char(eid));
        return (BAD);
    }

    OOR_LOG(LDBG_2, "Mapping with EID %s already exists, replacing!",
            lisp_addr_to_char(eid));

    map = mcache_entry_mapping(mce);

    /* DISCARD all locator state */
    mapping_update_locators(map, mapping_locators_lists(recv_map));

    /* Update forwarding info */
    tr->fwd_policy->updated_map_cache_inf(tr->fwd_policy_dev_parm,mce);

    return (GOOD);
}

void
tr_mcache_entry_program_timers(lisp_tr_t *tr, mcache_entry_t *mce )
{
    if (mcache_how_learned(mce) == MCE_DYNAMIC){
        /* Reprogramming timers */
        tr_mc_entry_program_expiration_timer(tr, mce);
    }

    /* RLOC probing timer */
    tr_program_mce_rloc_probing(tr, mce);
}

/*****************************************************************************/


inline mcache_entry_t *
get_proxy_etrs_for_afi(lisp_tr_t *tr, int afi)
{
   return (mcache_get_all_space_entry(tr->map_cache, afi));
}


lisp_addr_t *
get_map_resolver(lisp_tr_t *tr)
{
    glist_entry_t * it = NULL;
    lisp_addr_t * addr = NULL;
    oor_ctrl_t * ctrl = NULL;
    int supported_afis;

    ctrl = ctrl_dev_get_ctrl_t(tr_get_ctrl_device(tr));
    supported_afis = ctrl_supported_afis(ctrl);

    if ((supported_afis & IPv6_SUPPORT) != 0){
        glist_for_each_entry(it,tr->map_resolvers){
            addr = (lisp_addr_t *)glist_entry_data(it);
            if (lisp_addr_ip_afi(addr) == AF_INET6){
                return (addr);
            }
        }
    }

    if ((supported_afis & IPv4_SUPPORT) != 0){
        glist_for_each_entry(it,tr->map_resolvers){
            addr = (lisp_addr_t *)glist_entry_data(it);
            if (lisp_addr_ip_afi(addr) == AF_INET){
                return (addr);
            }
        }
    }

    OOR_LOG (LDBG_1,"get_map_resolver: No map resolver reachable");
    return (NULL);
}
