/*
 *
 * Copyright (C) 2017 Cisco Systems, Inc.
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

#include "encapsulations.h"
#include "vxlan-gpe.h"
#include "../../liblisp/liblisp.h"

uint16_t
get_encap_port(oor_encap_t encap)
{
    switch (encap) {
    case ENCP_LISP:
        return LISP_DATA_PORT;
    case ENCP_VXLAN_GPE:
        return VXLAN_GPE_DATA_PORT;
    default:
        return 0;
    }
}
