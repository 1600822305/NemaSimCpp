# Step 2: 核心类型与基础设施

> 日期: 2026-02-10
> 状态: ✅ 完成

---

## 目标

实现仿真所需的基础类型系统、配置解析和日志框架。

## 实现

### Vector2d (`core/types.h`)

2D 向量，用于身体段位置、环境坐标等：

```cpp
struct Vector2d {
    double x, y;
    // 运算符: +, -, *, /, +=, -=, *=
    double norm() / norm_sq() / dot() / cross();
    Vector2d normalized();
    static Vector2d from_angle(double angle);
};
```

### 枚举类型 (`core/types.h`)

| 枚举 | 值 | 用途 |
|------|---|------|
| `NeuronType` | SENSORY/INTER/MOTOR/PHARYNGEAL/UNKNOWN | 神经元功能分类 |
| `NeurotransmitterType` | ACh/GABA/Glu/DA/5-HT/TA/OA/UNKNOWN | 7 种已知递质 |
| `SensoryModality` | CHEMO_ATT/CHEMO_REP/MECHANO_ANT/POST/THERMO/PROPRIO/NOCI/NONE | 8 种感觉模态 |

### 数据结构 (`core/types.h`)

| 结构体 | 字段 | 用途 |
|--------|------|------|
| `NeuronInfo` | id, name, type, nt, modality, is_left, pair_id | 神经元描述 |
| `SynapseInfo` | pre_id, post_id, num_sections, nt | 化学突触描述 |
| `GapJunctionInfo` | neuron_a_id, neuron_b_id, num_sections | 电突触描述 |

### 全局常量

```cpp
NUM_SOMATIC_NEURONS = 302    // 体细胞系神经元
NUM_PHARYNGEAL_NEURONS = 20  // 咽部神经元
NUM_TOTAL_NEURONS = 322
NUM_BODY_SEGMENTS = 48       // 身体离散段数
NUM_MUSCLES = 95             // 体壁肌肉数
```

### Config (`core/config.h`)

INI 格式解析器，支持 `key = value` 行，`#` 注释。
API: `get_string()` / `get_double()` / `get_int()` / `get_bool()`。
用于仿真参数配置（dt、竞技场大小、数据文件路径等）。

### Logger (`core/logger.h`)

线程安全单例日志，4 级：DEBUG/INFO/WARN/ERROR。
输出到 stderr + 可选文件。带时间戳 (HH:MM:SS.mmm) + 文件名:行号。
宏接口：`LOG_DEBUG(...)` / `LOG_INFO(...)` / `LOG_WARN(...)` / `LOG_ERROR(...)`。

## 设计决策

- **NeuronId = int**: 简单高效，302 个神经元用 int 足够
- **枚举用 uint8_t**: 节省内存，每个神经元只需几个字节的元数据
- **header-only 优先**: 小型工具类直接在头文件实现，编译器可内联优化
