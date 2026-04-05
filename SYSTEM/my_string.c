#include "stm32f1xx_hal.h"

char *my_strchr(const char *s, int c)
{
    char ch = (char)c;

    while (*s != ch) {
        if (*s++ == '\0')
            return NULL;
    }
    return (*s == ch) ? (char *)s : NULL;
}

static inline int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

int32_t my_atoi(const char *s)
{
    int32_t  val = 0;
    int      neg = 0;

    /* 1. 跳过空白 */
    while (is_space(*s)) ++s;

    /* 2. 处理符号 */
    if (*s == '-') {
        neg = 1;
        ++s;
    } else if (*s == '+') {
        ++s;
    }

    /* 3. 转换 */
    while (*s >= '0' && *s <= '9') {
        int32_t digit = *s++ - '0';

        /* 溢出检查：正溢出 */
        if (val > (INT32_MAX - digit) / 10) {
            return neg ? INT32_MIN : INT32_MAX;
        }

        val = val * 10 + digit;
    }

    return neg ? -val : val;
}