/*
 * @Author: Renascent Adore lizaterop@gmail.com
 * @Date: 2026-11
 * @LastEditors: Renascent Adore lizaterop@gmail.com
 * @LastEditTime: 2026-11
 * @Description: cc_hash 测试。
 * Copyright (c) 2026 by lizaterop@gmail.com, All Rights Reserved.
 */

#include "test_header.h"

#include <ccalloc.h>
#include <cchost.h>
#include <string.h>

static void hash_check_hex(
    const char *label, const CCMd5Digest *digest, const char *hex
)
{
    CCUINT8 expected[CC_MD5_DIGEST_SIZE];
    for (CCUINT32 i = 0; i < CC_MD5_DIGEST_SIZE; i++)
    {
        CCUINT32 high = 0;
        CCUINT32 low = 0;
        char a = hex[i * 2];
        char b = hex[i * 2 + 1];
        if (a >= 'a')
        {
            high = (CCUINT32)(a - 'a' + 10);
        }
        else
        {
            high = (CCUINT32)(a - '0');
        }
        if (b >= 'a')
        {
            low = (CCUINT32)(b - 'a' + 10);
        }
        else
        {
            low = (CCUINT32)(b - '0');
        }
        expected[i] = (CCUINT8)((high << 4) | low);
    }
    CC_CHECK_MEM(label, expected, CC_MD5_DIGEST_SIZE,
        digest->bytes, CC_MD5_DIGEST_SIZE);
}

static CCINT32 hash_memory(
    const void *data, CCUINT64 size, CCMd5Digest *digest
)
{
    CCHashInput input;
    memset(&input, 0, sizeof(input));
    input.data = data;
    input.size = size;
    return cc_md5(&input, digest);
}

void test_cc_hash(void)
{
    CC_CASE("HASH-001", "MD5 标准向量");
    {
        CCMd5Digest digest;
        CC_CHECK_EQ_I64("空串", CC_FS_OK,
            hash_memory("", 0, &digest));
        hash_check_hex("空串摘要", &digest,
            "d41d8cd98f00b204e9800998ecf8427e");

        CC_CHECK_EQ_I64("abc", CC_FS_OK,
            hash_memory("abc", 3, &digest));
        hash_check_hex("abc 摘要", &digest,
            "900150983cd24fb0d6963f7d28e17f72");

        const char *text = "message digest";
        CC_CHECK_EQ_I64("长串", CC_FS_OK,
            hash_memory(text, (CCUINT64)strlen(text), &digest));
        hash_check_hex("长串摘要", &digest,
            "f96b697d7cb7938d525a2f31aaf161d0");
    }
    CC_CASE_END();

    CC_CASE("HASH-002", "内存与 VFS 一致");
    {
        const char *root = "out/hash_test_tmp";
        cchost_mkdirs(root);

        CCFiles *fs = ccfiles_create();
        CC_CHECK(fs != NULL, "创建探针");
        if (fs && ccfiles_mount(
                fs, "/m", root, CC_FS_MOUNT_DIR
            ) == CC_FS_OK)
        {
            CC_CHECK_EQ_I64("创建文件", CC_FS_OK,
                ccfiles_create_file(fs, "/m/a.bin"));
            CC_CHECK_EQ_I64("写入", 3,
                ccfiles_write(fs, "abc", 0, 3));
            CC_CHECK_EQ_I64("flush", CC_FS_OK, ccfiles_flush(fs));

            CCHashInput input;
            memset(&input, 0, sizeof(input));
            input.fs = fs;
            input.v_path = "/m/a.bin";
            CCMd5Digest vfs_digest;
            CC_CHECK_EQ_I64("VFS 摘要", CC_FS_OK,
                cc_md5(&input, &vfs_digest));
            hash_check_hex("VFS 摘要值", &vfs_digest,
                "900150983cd24fb0d6963f7d28e17f72");

            CCMd5Digest mem_digest;
            CC_CHECK_EQ_I64("内存摘要", CC_FS_OK,
                hash_memory("abc", 3, &mem_digest));
            CC_CHECK_MEM("两种模式一致",
                mem_digest.bytes, CC_MD5_DIGEST_SIZE,
                vfs_digest.bytes, CC_MD5_DIGEST_SIZE);
        }
        if (fs)
            ccfiles_destroy(fs);
        cchost_remove("out/hash_test_tmp/a.bin");
        cchost_remove(root);
    }
    CC_CASE_END();

    CC_CASE("HASH-003", "data 优先");
    {
        CCHashInput input;
        memset(&input, 0, sizeof(input));
        input.data = "abc";
        input.size = 3;
        input.fs = NULL;
        input.v_path = "/missing";
        CCMd5Digest digest;
        CC_CHECK_EQ_I64("内存优先", CC_FS_OK, cc_md5(&input, &digest));
        hash_check_hex("摘要值", &digest,
            "900150983cd24fb0d6963f7d28e17f72");
    }
    CC_CASE_END();

    CC_CASE("HASH-004", "参数判定");
    {
        CCMd5Digest digest;
        CCHashInput input;
        memset(&input, 0, sizeof(input));
        CC_CHECK_EQ_I64("两者皆空", CC_FS_ERR_PARAM,
            cc_md5(&input, &digest));

        input.v_path = "/x";
        CC_CHECK_EQ_I64("只有路径", CC_FS_ERR_PARAM,
            cc_md5(&input, &digest));

        memset(&input, 0, sizeof(input));
        input.fs = (CCFiles *)1;
        CC_CHECK_EQ_I64("只有探针", CC_FS_ERR_PARAM,
            cc_md5(&input, &digest));

        CC_CHECK_EQ_I64("空输出", CC_FS_ERR_PARAM,
            hash_memory("abc", 3, NULL));
    }
    CC_CASE_END();

    CC_CASE("HASH-005", "VFS 目录与缺失");
    {
        CCFiles *fs = ccfiles_create();
        CC_CHECK(fs != NULL, "创建探针");
        if (fs && ccfiles_mount(
                fs, "/m", "out", CC_FS_MOUNT_DIR
            ) == CC_FS_OK)
        {
            CCHashInput input;
            memset(&input, 0, sizeof(input));
            input.fs = fs;
            input.v_path = "/m";
            CCMd5Digest digest;
            CC_CHECK_EQ_I64("目录", CC_FS_ERR_ISDIR,
                cc_md5(&input, &digest));

            input.v_path = "/m/__missing_hash__";
            CC_CHECK_EQ_I64("缺失", CC_FS_ERR_NOENT,
                cc_md5(&input, &digest));
        }
        if (fs)
            ccfiles_destroy(fs);
    }
    CC_CASE_END();
}
