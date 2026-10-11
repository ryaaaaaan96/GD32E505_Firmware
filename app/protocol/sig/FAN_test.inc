/* 测点清单：描述、默认值及字段规则只在文件作用域展开，存储期为静态。
 * 主站采集 TEST_001～030，TEST_031～050 保留为本地读写测试。
 */
ABUS_PARAMS(FAN_TEST_001,
    ABUS_PARAM(FAN_TEST_001_U32,
        .offset = offsetof(FANTest_t, u32_value),
        .type = ALIB_DATA_U32,
        .size = sizeof(uint32_t),
        .range = &(const aBusRange_t){
            .min.u32 = 0U, .max.u32 = 6000U
        }
    )
    ABUS_PARAM(FAN_TEST_001_S32,
        .offset = offsetof(FANTest_t, s32_value),
        .type = ALIB_DATA_S32,
        .size = sizeof(int32_t)
    )
)

ABUS_BOUND_SIG(TEST_001, 2001U, FANTest_t,
    .type = ALIB_DATA_STRUCT,
    .flags = ABUS_SIG_FLAG_LOCK | ABUS_SIG_FLAG_DISJOINT,
    .default_data = &(const FANTest_t){100U, 25},
    ABUS_PARAM_REF(FAN_TEST_001)
)

/* 本地可写测试配置；主站下一轮采集会覆盖本地值，写入不自动下发。 */
ABUS_SIG(TEST_002, 2002U, uint8_t,
    .type = ALIB_DATA_U8,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint8_t){0U},
    .range = &(const aBusRange_t){.min.u8 = 0U, .max.u8 = 1U}
)
ABUS_SIG(TEST_003, 2003U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){1500U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 6000U}
)
ABUS_SIG(TEST_004, 2004U, uint16_t,
    .type = ALIB_DATA_U16,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint16_t){0U},
    .range = &(const aBusRange_t){.min.u16 = 0U, .max.u16 = 3U}
)

ABUS_SIG(TEST_005, 2005U, int32_t,
    .type = ALIB_DATA_S32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const int32_t){-5},
    .range = &(const aBusRange_t){.min.s32 = -1000, .max.s32 = 1000}
)
ABUS_SIG(TEST_006, 2006U, FANTestRaw_t,
    .type = ALIB_DATA_RAW,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const FANTestRaw_t){1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U}
)
ABUS_PARAMS(FAN_TEST_007,
    ABUS_PARAM(FAN_TEST_007_U32,
        .offset = offsetof(FANTest_t, u32_value),
        .type = ALIB_DATA_U32, .size = sizeof(uint32_t),
        .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 6000U}
    )
    ABUS_PARAM(FAN_TEST_007_S32,
        .offset = offsetof(FANTest_t, s32_value),
        .type = ALIB_DATA_S32, .size = sizeof(int32_t),
        .range = &(const aBusRange_t){.min.s32 = -1000, .max.s32 = 1000}
    )
)
ABUS_SIG(TEST_007, 2007U, FANTest_t,
    .type = ALIB_DATA_STRUCT,
    .flags = ABUS_SIG_FLAG_LOCK | ABUS_SIG_FLAG_DISJOINT,
    .default_data = &(const FANTest_t){100U, 25},
    ABUS_PARAM_REF(FAN_TEST_007)
)
ABUS_SIG(TEST_008, 2008U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){8U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_009, 2009U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){9U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_010, 2010U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){10U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_011, 2011U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){11U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_012, 2012U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){12U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_013, 2013U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){13U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_014, 2014U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){14U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_015, 2015U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){15U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_016, 2016U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){16U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_017, 2017U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){17U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_018, 2018U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){18U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_019, 2019U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){19U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_020, 2020U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){20U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_021, 2021U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){21U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_022, 2022U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){22U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_023, 2023U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){23U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_024, 2024U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){24U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_025, 2025U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){25U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_026, 2026U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){26U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_027, 2027U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){27U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_028, 2028U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){28U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_029, 2029U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){29U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_030, 2030U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){30U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_031, 2031U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){31U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_032, 2032U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){32U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_033, 2033U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){33U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_034, 2034U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){34U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_035, 2035U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){35U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_036, 2036U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){36U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_037, 2037U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){37U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_038, 2038U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){38U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_039, 2039U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){39U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_040, 2040U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){40U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_041, 2041U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){41U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_042, 2042U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){42U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_043, 2043U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){43U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_044, 2044U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){44U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_045, 2045U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){45U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_046, 2046U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){46U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_047, 2047U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){47U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_048, 2048U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){48U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_049, 2049U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){49U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
ABUS_SIG(TEST_050, 2050U, uint32_t,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .default_data = &(const uint32_t){50U},
    .range = &(const aBusRange_t){.min.u32 = 0U, .max.u32 = 10000U}
)
