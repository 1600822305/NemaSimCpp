# Step 29: 本体感觉波传播 (Proprioceptive Wave Propagation)

> 日期: 2026-02-10
> 状态: ✅ 完成

---

## 目标

将 C. elegans 的运动模式从"摇头"（全身同相弯曲）升级为更真实的"S 波"——通过本体感觉顺序接力实现从头到尾的弯曲波传播。

## 文献基础

| 文献 | 关键发现 |
|------|----------|
| Wen 2012 Neuron | B 类运动神经元自身转导本体感觉信号；切断前部→后部停止弯曲 |
| Boyle 2012 Frontiers | 双稳态 B 类 + 拉伸受体沿轴突整合；环境拖曳力创造相位延迟 |
| Yeon 2018 PLOS Biology | SMDD 是头部转向本体感觉神经元 (TRP-1/TRP-2 通道) |

### 关键生物学参数 (Boyle 2012)

- 12 神经单元 × 4 体节 = 48 段
- 肌肉时间常数 τ_M = 100ms
- SR 电导从头到尾线性增加（补偿振幅梯度）
- 爬行波长 ≈ 0.65 体长, 频率 ≈ 0.5Hz, 速度 ≈ 0.15-0.2 mm/s
- D 类 GABA 交叉抑制：对游泳必需，对爬行非必需

## 决策

| 决策 | 选择 | 理由 |
|------|------|------|
| B 类映射 | 顺序感知前一单元领地 | Wen 2012: 后部弯曲需要前部弯曲 |
| A 类映射 | 保持原始同步 (seg 0/5/15) | 提供基础肌肉驱动力，不可修改 |
| 曲率扩散 | 0.5 (适度弹性耦合) | Boyle 2012: 体节间连续体弹性 |
| ProprioMapping 扩展 | 添加 sense_start/end 字段 | 预留多段整合能力 |

## 实现

### B 类顺序感知映射

每个 B 类运动神经元感知**前一个单元领地内部**的曲率，产生 D/V 交替接力 S 波：

```
SMD 振荡 (seg 0-3)
  → DB01 感知 seg 2 (SMD 领地) → DB01 dorsal 激活 (seg 4-9) → +曲率
    → VB02 感知 seg 7 (DB01 领地) → VB02 ventral 激活 (seg 10-19) → -曲率
      → DB03 感知 seg 15 (DB02 领地) → DB03 dorsal 激活 (seg 20-29) → +曲率

相位链: DB01(+) → VB02(-) → DB03(+) = S 波
```

```cpp
// simulation_engine.cpp — B 类顺序感知
add_pm("DB01", 2,  0,  4,  true);   // SMD 领地 (seg 0-3)
add_pm("DB02", 7,  4,  10, true);   // DB01/VB01 领地
add_pm("DB03", 15, 10, 20, true);   // DB02/VB02 领地
add_pm("VB01", 2,  0,  4,  false);
add_pm("VB02", 7,  4,  10, false);
add_pm("VB03", 15, 10, 20, false);
// A 类: 保持原始同步映射
add_pm("DA01", 0, ...); add_pm("DA02", 5, ...); add_pm("DA03", 15, ...);
add_pm("VA01", 0, ...); add_pm("VA02", 5, ...); add_pm("VA03", 15, ...);
```

### 体节间曲率扩散

```cpp
// body_model.cpp — 被动弹性耦合
double curv_left  = (i > 0) ? segments_[i-1].curvature : seg.curvature;
double curv_right = (i < N-1) ? segments_[i+1].curvature : seg.curvature;
double diffusion = curvature_diffusion_ * (curv_left - 2*seg.curvature + curv_right);
dcurv += diffusion;

// body_model.h
double curvature_diffusion_ = 0.5; // Boyle 2012: 弹性连续体耦合
```

### ProprioMapping 结构体扩展

```cpp
// simulation_engine.h
struct ProprioMapping {
    int neuron_id;
    int sample_segment;   // 主感知点
    int sense_start;      // 多段整合范围起始 (预留)
    int sense_end;        // 多段整合范围结束 (预留)
    bool is_dorsal;
};
```

## 调试过程中的关键发现

### 根因：A 类映射不可修改

**现象**: 修改 A 类映射后速度从 0.3 骤降到 0.1 mm/s（-67%）

**根因分析**:
- 原始系统中 A 类 (DA/VA) 与 B 类 (DB/VB) 使用相同映射点 (seg 0/5/15)
- A 类在前进运动期间仍接收本体感觉输入并产生肌肉激活
- 修改 A 类映射 → 部分 A 类神经元无法感知曲率 → 肌肉驱动减半 → 速度骤降

**教训**: A 类映射提供基础肌肉驱动力，在前进模式中也有贡献，不可随意修改。

### 鸡生蛋问题

将 B 类感知点设在**自身领地边界** (如 DB01→seg4) 而非**前一单元领地内部** (DB01→seg2)
会导致死锁：神经元需要曲率才能激活，但曲率只来自自身肌肉。

### 曲率数值不稳定 (Forward Euler)

**现象**: diag 300s 速度仅 0.076 mm/s（regtest 30s 显示 0.3）

**根因分析**:
- 曲率更新使用 Forward Euler: `curv += (stiffness*(target-curv) - damping*curv) * dt`
- 稳定性条件: `(stiffness+damping)*dt < 2` → `10.5*0.5 = 5.25 >> 2` → **严重不稳定**
- 曲率每步在 ±3（clamp 上限）之间振荡（1000 Hz 噪声）
- 背腹两侧 MEC 通道都看到交替的 0/3.0 stretch → 都趋向 m≈0.5
- 背腹肌肉**同时激活** → |d-v| ≈ 0 → muscle_work ≈ 0 → 速度 ≈ 0

**修复**: 半隐式 Euler（对 stiffness+damping 项隐式处理，无条件稳定）:
```cpp
double denom = 1.0 + (stiffness_ + damping_) * dt;
seg.curvature = (seg.curvature + dt * (stiffness_ * target + diffusion)) / denom;
```

**效果**: diag speed 0.076 → 0.192 mm/s (+153%)

## 文件变更

```
src/simulation/simulation_engine.cpp  — B 类顺序感知映射 + apply_proprioceptive_stretch
src/simulation/simulation_engine.h   — ProprioMapping 扩展 (sense_start/end)
src/body/body_model.cpp              — 半隐式 Euler 曲率动力学 + 体节间扩散
src/body/body_model.h                — curvature_diffusion_ 成员变量
```

## 验证

```
regtest 30s: 14 pass, 0 FAIL (3 次运行均稳定)

Speed mean:     0.3 mm/s    (baseline 0.3, within tolerance)
Heading rate:   17.1 deg/s  (baseline 15.0, within tolerance)

diag 300s:
Speed mean:     0.192 mm/s  (从 0.076 提升 153%)
Heading rate:   5.15 deg/s
CI:            -0.31        (病原体学习预期行为)
Curvature:      0.2 /mm     (正常)
SMD swing:      60-80 mV    (正常)
```

## 参考文献

- Wen Q et al. (2012) Proprioceptive coupling within motor neurons drives C. elegans forward locomotion. *Neuron* 76(4):750-761
- Boyle JH et al. (2012) Gait modulation in C. elegans: an integrated neuromechanical model. *Frontiers in Computational Neuroscience* 6:10
- Yeon J et al. (2018) A sensory-motor neuron type mediates proprioceptive coordination of steering in C. elegans via two TRPC channels. *PLOS Biology* 16(6):e2004929
