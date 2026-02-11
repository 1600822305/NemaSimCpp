# Step 52: 重构 — ids_ map 自动注册

## 动机

`simulation_engine.h` 中有 **38 个**手动缓存的神经元 ID 字段：
- 17 个 `int xxx_id_ = -1` 单体字段
- 21 个 `std::vector<int> xxx_ids_` 分组字段

这些字段分散在 3 个初始化循环（`initialize_default()` 两处 + `cache_neuron_ids_and_synapses()`）中填充，
新增神经元时需要同时修改 header 声明 + 初始化代码，容易遗漏造成初始化顺序 bug。

## 方法

### 1. 两个 map + 两个访问器

```cpp
// simulation_engine.h
std::unordered_map<std::string, int> nid_;           // "AVAL" → 15
std::unordered_map<std::string, std::vector<int>> nids_;  // "MC" → {120,121}

int nid(const char* name) const;          // 单体查询
const std::vector<int>& nids(const char* key) const;  // 分组查询
```

### 2. 统一自动注册

`cache_neuron_ids_and_synapses()` 中一次性完成：
1. `nid_`: 遍历 connectome 所有神经元，按精确名注册
2. `nids_`: 19 个前缀组 + 1 个复合组 (head_motor = SMD+RMD)，一次循环填充
3. 类型指针缓存（smd_scn_[], ria_mcn_[]）也使用 `nid()` 查询

### 3. 全局替换

| 旧模式 | 新模式 | 示例 |
|--------|--------|------|
| `aval_id_` | `nid("AVAL")` | 单体 ID |
| `mc_ids_` | `nids("MC")` | 分组 ID |
| `head_motor_ids_` | `nids("head_motor")` | 复合组 |

## 文件变化

| 文件 | 变化 |
|------|------|
| `simulation_engine.h` | 移除 38 个字段，添加 2 个 map + 2 个访问器 |
| `simulation_engine.cpp` | 移除 3 个手动缓存循环 (~50行)，重写 cache 函数 |
| `setup_neuromodulation.cpp` | 替换 aib_ids_/aiz_ids_/nsml_id_/nsmr_id_ |
| `update_internal_states.cpp` | 替换 ric_ids_/aval_id_/dva_id_/ris_id_/mc_ids_/head_motor_ids_ |
| `update_pharynx_system.cpp` | 替换 mc_ids_/m3_ids_/m4_id_/i1_ids_ |

## 验证

- **编译**: 零错误
- **regtest**: 17 pass, 0 FAIL
- **新增神经元流程**: 只需在 `connectome_builder.cpp` 添加 `b.neuron()` 行，
  所有 ID 自动注册，无需修改 header 或初始化代码
