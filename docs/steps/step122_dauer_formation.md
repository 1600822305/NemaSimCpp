# Step 122: Dauer 形成 (Dauer Formation)

> 日期: 2026-02-13

---

## 动机

Dauer 是 C. elegans 在恶劣环境（缺食、高密度、高温）下进入的替代发育状态。Dauer 幼虫停止进食、降低运动、增强应激抗性，可存活数月。这一决策由 DAF-2/DAF-16 胰岛素通路和 DAF-7/TGF-β 通路整合环境信号做出。

## 生物学基础

### Dauer 决策信号通路
```
环境信号:
  食物匮乏 → ASI DAF-7(TGF-β)↓ + DAF-28(insulin)↓
  高信息素 → ASJ/ASI 检测拥挤
  高温(≥25°C) → 温度应激

信号整合:
  DAF-7↓ + DAF-28↓ → DAF-2 受体不激活
  → DAF-16/FOXO 进入核内
  → 启动 dauer 发育程序

正常生长 (有食物):
  DAF-7↑ + DAF-28↑ → DAF-2 活跃
  → DAF-16 留在细胞质
  → 正常生殖发育
```

### 关键神经元
- **ASI**: 分泌 DAF-7/TGF-β 和 DAF-28/insulin — 食物存在时高表达
- **ASJ**: 检测 dauer 信息素 — 介导 dauer 进入/退出
- **AWC**: 食物气味 — 通过 ASI 调制 dauer 决策

### 参考文献
- Golden & Riddle 1984 — 发现 dauer 决策三因素 (食物/信息素/温度)
- Hu 2007 PLoS Genet — DAF-7/DAF-28 从 ASI 分泌
- Fielenbach & Antebi 2008 — DAF-2/DAF-16 胰岛素/FOXO 通路综述
- Riddle 1988 — dauer 分子遗传学

## 实现细节

### DAF-7/DAF-28 从 ASI
```cpp
daf7_target = (food_here > 0.1) ? 1.0 : 0.1   // 食物→高，无食物→低
daf28_target = satiety_                          // 饱食→高，饥饿→低
// 30s 积分时间常数
```

### Dauer 信号整合
```cpp
food_pro_dauer = 1.0 - 0.5 × (daf7 + daf28)    // [0,1]
pheromone_pro_dauer = phero / (phero + 0.3)      // 饱和
temp_pro_dauer = clamp((T - 25°C) / 2°C, 0, 1)  // ≥25°C

pro_dauer = 0.60 × food + 0.25 × pheromone + 0.15 × temperature
dauer_signal += (pro_dauer - dauer_signal) × dt / 60000ms
```

### Dauer 行为效应 (dauer_signal > 0.3, 渐变)
1. **MC 抑制** (-30pA × strength): 咽泵停止（口腔封闭）
2. **AVB 抑制** (-8pA × strength): 运动减少（能量保存）
3. **ASJ 增强** (+5pA × strength): 增强信息素检测（退出条件感知）

### 阈值
- dauer_signal > 0.8: `is_dauer()` = true（完全 dauer 状态）
- dauer_signal 0.3-0.8: 渐变过渡效应

## Diag 验证

### 有食物 (默认)
```
38. DAUER FORMATION (Step 122):
   dauer_signal=0.205  is_dauer=no
   DAF-7/TGF-b: HIGH (reproductive)
```

### 无食物 (--no-food)
```
38. DAUER FORMATION (Step 122):
   dauer_signal=0.426  is_dauer=no
   DAF-7/TGF-b: LOW (pro-dauer)
```

无食物时 dauer 信号升高（0.205→0.426），但 120s 仿真不足以完全进入 dauer（需要更长饥饿期）。

## 修改文件
- `src/simulation/simulation_engine.h`: dauer 状态变量和函数声明
- `src/simulation/update_internal_states.cpp`: update_dauer_decision() + apply_dauer_effects()
- `src/simulation/simulation_engine.cpp`: 在 step() 中调用 dauer 函数
- `src/simulation/diag_main.cpp`: dauer 诊断输出
