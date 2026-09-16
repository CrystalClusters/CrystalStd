#include <cc_atoi.h>
#include <color_print.h>

/**
 * 不符合数字格式的，返回0。
 * 符合数字格式，但是太大了的，返回 INT32_MAX。
 * 符合数字格式，但是太小了的，返回 INT32_MIN。
 * 正常情况下返回转化好的整数值。
 */
CCINT32 cc_atoi(const char *str_num)
{
    if (!str_num) return 0;
    CCINT64 num = 0;
    CCINT64 digit = 0;
    CCINT64 pow = 1;
    CCBOOL pow_overflow = CCFALSE;
    if (*str_num == '-')
    {
        pow = -1;
        str_num++;
    }
    const char *cursor = str_num;
    while (*cursor != '\0')
    {
        if (*cursor < '0' || *cursor > '9')
        {
            color_print(CC_TEXT_COLOR(CC_RED, CC_DEFAULT) "非数字字符：%c\n", *cursor);
            return 0;
        }
        cursor++;
    }
    // 指向最后一个字符
    cursor--;
    while (cursor >= str_num)
    {
        digit = *cursor - '0';
        // 位权越界
        if (digit > 0 && pow_overflow)
        {
            if (pow < 0)
            {
                color_print(CC_TEXT_COLOR(CC_YELLOW, CC_DEFAULT) "数值过小，返回边界值：INT32_MIN\n");
                return INT32_MIN;
            }
            color_print(CC_TEXT_COLOR(CC_YELLOW, CC_DEFAULT) "数值过大，返回边界值：INT32_MAX\n");
            return INT32_MAX;
        }
        num += pow * digit;
        if (num > INT32_MAX)
        {
            color_print(CC_TEXT_COLOR(CC_YELLOW, CC_DEFAULT) "数值过大，返回边界值：INT32_MAX\n");
            return INT32_MAX;
        }
        if (num < INT32_MIN)
        {
            color_print(CC_TEXT_COLOR(CC_YELLOW, CC_DEFAULT) "数值过小，返回边界值：INT32_MIN\n");
            return INT32_MIN;
        }
        // 防止 pow 溢出检测
        if (!pow_overflow)
        {
            if (pow > INT32_MAX / 10 || pow < INT32_MIN / 10)
                pow_overflow = CCTRUE;
            else
                pow *= 10;
        }
        cursor--;
    }
    return num;
}
