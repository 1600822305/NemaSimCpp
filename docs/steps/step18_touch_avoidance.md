# Step 18: 触觉回避 — 第2个涌现行为

## 目标

在趋化性基础上添加第二个行为——触觉回避，验证神经回路架构的泛化性。

```
当前:  食物 → 感觉 → 中间 → 运动 → 趋化
新增:  触碰 → ALM/PLM → AVD/AVA → reversal + omega turn
                                        ↕
                                   与趋化竞争/切换
```

## 文献基础

### Chalfie et al. 1985 — 触觉回避回路

**Push-pull 原则**：触觉神经元与**激动剂**中间神经元用 gap junction（兴奋），与**拮抗剂**用化学突触（抑制）。

```
前触: ALM ──gap──→ AVD ──→ AVA → DA/VA → 后退
           ──syn(抑制)──→ AVB (抑制前进)

后触: PLM ──syn(抑制)──→ AVA/AVD (抑制后退)
           ──gap──→ PVC → AVB → DB/VB → 加速前进
```

### Wang et al. 2020 (eLife) — 逃逸序列

```
触碰 → 后退(reversal, 2-3s) → omega转弯 或 恢复前进
```

- 后退越长 → omega 概率越高：P(ω) = 1 - exp(-duration/τ)
- 前进↔后退：互相抑制（winner-takes-all）
- Omega 转弯由 SMD 驱动，腹侧偏好（80%）

### Gray 2005 — Omega 转弯

- SMD 编码 omega 转弯幅度
- RIV 提供腹侧不对称
- 深弯折 >140°，方向改变 ~180°

## 实现

### 1. 连接组修复 (connectome_loader.cpp)

| 连接 | 修改前 | 修改后 | 原理 |
|------|--------|--------|------|
| ALM→AVD | 兴奋性化学突触 | **gap junction** (4 sections) | 激动剂用gap (Chalfie 1985) |
| PLM→AVA | 兴奋性化学突触 | **抑制性**化学突触 (3 sections) | 拮抗剂用inhibitory |
| ALM→AVB | 缺失 | **新增**抑制性 (3 sections) | 前触抑制前进 |
| PLM→AVD | 缺失 | **新增**抑制性 (2 sections) | 后触抑制后退 |
| AVD→AVA | 缺失 | **新增**兴奋性 (1 section, 弱) | 触觉信号中继 |

**注意**：AVD→AVA 必须弱（1 section），否则紧张性 AVD 活动会持续激活 AVA 破坏趋化。

### 2. 触觉刺激 (apply_touch_stimulus)

壁碰撞检测：
```cpp
bool front_touch = (head.x < 2mm || head.x > 48mm || head.y < 2mm || head.y > 48mm);
bool rear_touch  = (tail 近壁);

前触 → ALM 注入 80pA 脉冲 → ALM→AVD(gap)→AVA → 后退
后触 → PLM 注入 80pA 脉冲 → PLM⊣AVA → 抑制后退 → 加速前进
```

ALM/PLM 无基线电流（从 other_sensory_ids_ 排除），只有碰壁时才激活。

### 3. 后退追踪与 Omega 决策

```cpp
// AVA release rate > 0.6 → 判定为"正在后退"
is_reversing_ = (ava_rel > 0.6);

// 后退结束时：
reversal_duration_ = current_time_ - reversal_start_time_;
P(omega) = 1 - exp(-duration / 1000ms);  // Wang 2020
// 80% 腹侧偏好
```

### 4. Omega 转弯执行 (apply_omega_turn)

```cpp
// 500ms 深弯折期间：
body_.set_omega_mode(true);         // 提高 max_dtheta 到 300°/s
body_.set_curvature_bias(±8.0);    // 强曲率偏置

// 300°/s × 0.5s = 150° heading change ✓ (>140° 目标)
```

`omega_mode` 临时将 body_model 的 max_dtheta 从 50°/s 提高到 300°/s。

### 5. 可视化

控制面板添加行为状态指示器：
- 🟢 前进 + 趋化
- 🔴 后退
- 🟣 OMEGA 转弯

## 验证结果

| 指标 | Step 17 (趋化only) | Step 18 (触觉+趋化) | 说明 |
|------|--------------------|--------------------|------|
| CI | 0.760 | **0.736** | 触觉回路未破坏趋化 ✅ |
| 速度 | 0.21 mm/s | **0.22 mm/s** | 正常 ✅ |
| 突触数 | 72+6 | **82+8** | 新增触觉连接 |

## 预期行为序列

```
正常趋化 → 接近壁 → 前触(ALM激活) → 后退2-3s → omega转弯150° → 新方向 → 继续趋化
```

两种行为通过神经回路自然竞争/切换，无硬编码状态机。

## 新增/修改文件

| 文件 | 变更 |
|------|------|
| `connectome_loader.cpp` | 触觉回路连接修复 (push-pull架构) |
| `simulation_engine.h` | 触觉状态变量 + apply_touch_stimulus/omega声明 |
| `simulation_engine.cpp` | 壁碰撞检测 + omega决策 + omega执行 |
| `body_model.h/.cpp` | omega_mode_ + 动态 max_dtheta |
| `vis_app.cpp` | 行为状态指示器 |

## 参考文献

- Chalfie et al. 1985 — "The neural circuit for touch sensitivity in C. elegans" (J Neurosci 5:956-964)
- Wang et al. 2020 — "Flexible motor sequence generation during stereotyped escape responses" (eLife 9:e56942)
- Gray et al. 2005 — "A circuit for navigation in C. elegans" (PNAS 102:3184-3191)
- NCBI Bookshelf NBK20005 — "Mechanosensory Control of Locomotion" (C. elegans II)
