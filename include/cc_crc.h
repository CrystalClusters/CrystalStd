/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: CRC-32 校验计算
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved. 
 */

#ifndef _INCLUDE_CC_CRC_H_
#define _INCLUDE_CC_CRC_H_

#include "crystal_cluster.h"

/**
 * CRC-32（IEEE 802.3）校验计算。
 * data：数据起始地址；size：字节数；seed：续算种子（首块传 0）。
 * 分块续算：cc_crc32(B, nb, cc_crc32(A, na, 0)) == cc_crc32(A+B, na+nb, 0)。
 * 空指针且 size 非 0 返回 0；size 为 0 返回 seed。
 */
CCUINT32 cc_crc32(const void *data, CCUINT64 size, CCUINT32 seed);

#endif
