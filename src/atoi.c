#include <cc_atoi.h>
#include <color_print.h>

/**
 * 不符合数字格式的，返回0 
 * 符合数字格式，但是太大了的，返回 INT32_MAX
 * 正常情况下返回转化好的整数值
 */
CCINT32 cc_atoi(const char *str_num)
{
    if (!str_num) return 0;
    CCINT64 num = 0;
    CCINT64 pow = 1;
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
        num += pow * (*cursor - '0');
        if (num > INT32_MAX)
        {
            color_print(CC_TEXT_COLOR(CC_YELLOW, CC_DEFAULT) "数值过大，返回边界值：INT32_MAX\n");
            num = INT32_MAX;
            break;
        }
        if (num < INT32_MIN)
        {
            color_print(CC_TEXT_COLOR(CC_YELLOW, CC_DEFAULT) "数值过小，返回边界值：INT32_MIN\n");
            num = INT32_MIN;
            break;
        }
        pow *= 10;
        cursor--;
    }
    return num;
}