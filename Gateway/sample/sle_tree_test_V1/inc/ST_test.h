/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * Description: SLE tree test sample.
 */

#ifndef SLE_TREE_TEST_H
#define SLE_TREE_TEST_H

#include "errcode.h"

typedef enum {
    SLE_TREE_ROLE_ROOT = 1,
    SLE_TREE_ROLE_RELAY = 2,
    SLE_TREE_ROLE_LEAF = 3,
} sle_tree_role_t;

errcode_t sle_tree_test_init_with_role(sle_tree_role_t role);
errcode_t sle_tree_test_root_init(void);
errcode_t sle_tree_test_relay_init(void);
errcode_t sle_tree_test_leaf_init(void);

#endif
