#include <cc_wildcard.h>
#include <ccalloc.h>
#include <string.h>

/**
 * DP 滚动数组实现。
 * dp[j] 表示「当前模式前缀」是否匹配 str 的前 j 个字符。
 * 时间 O(pattern_len * str_len)，空间 O(str_len)。
 */
CCBOOL cc_wildcard(const char *pattern, const char *str)
{
    if (!pattern || !str) return CCFALSE;

    CCUINT32 pattern_len = (CCUINT32)strlen(pattern);
    CCUINT32 str_len = (CCUINT32)strlen(str);

    CCUINT8 *dp = (CCUINT8 *)ccalloc(NULL, (CCUINT64)str_len + 1, CC_DBG_REL_TAG("cc_wildcard dp", NULL));
    if (!dp) return CCFALSE;
    memset(dp, 0, (CCUINT64)str_len + 1);

    // 空模式匹配空串
    dp[0] = 1;
    for (CCUINT32 i = 1; i <= pattern_len; i++)
    {
        // prev 始终保存「上一行」dp[j] 的值（dp[j] 更新前先取走）
        CCUINT8 prev = dp[0];
        // 空串匹配：仅当当前模式字符为 '*' 时沿用上一行
        dp[0] = (pattern[i - 1] == '*') ? dp[0] : 0;
        for (CCUINT32 j = 1; j <= str_len; j++)
        {
            CCUINT8 tmp = dp[j];
            if (pattern[i - 1] == '*')
                dp[j] = dp[j] || dp[j - 1];
            else if (pattern[i - 1] == '?' || pattern[i - 1] == str[j - 1])
                dp[j] = prev;
            else
                dp[j] = 0;
            prev = tmp;
        }
    }

    CCBOOL matched = dp[str_len] ? CCTRUE : CCFALSE;
    ccalloc(dp, 0, NULL);
    return matched;
}
