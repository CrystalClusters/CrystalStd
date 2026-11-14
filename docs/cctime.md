# cctime

水晶簇标准组件中和时间控制相关的部分。

## 1.接口列表

```C
// 获取高精度相对时间戳
double cctime();

// 高精度休眠
void ccsleep(CCUINT32 us);

// 日期获取
CCDate ccdate();
```

**参数含义：**
- `us`：时长（微秒）

**返回值：**
- **cctime：**
时间戳，单位为秒。
- **ccsleep：**
无
- **ccdate：**
精确到毫秒的日期时间戳

**注意事项：**
- cctime 返回的时间戳是相对时间，每次重启程序的零点参考都可能不同，不可用于绝对日期时间计算。 

## 2.结构体定义

```C
// 日期结构体
typedef struct cc_date {
    CCINT16 year;  //年
    CCINT16 month; //月份
    CCINT16 dweek; //周
    CCINT16 day;   //日
    CCINT16 hour;  //时
    CCINT16 min;   //分
    CCINT16 sec;   //秒
    CCINT16 ms;    //毫秒
} CCDate;
```