# Step 13: 技术债务清理 — 生物学机制替换占位符

> 日期: 2026-02-10
> 状态: ✅ 完成
> 前置: Step 12 (运动驱动占位符)

## 目标

清除 `00_design_principles.md` 中标识的 4 项技术债务 (TD-01 ~ TD-04)，
将 Step 12 的硬编码占位符替换为生物学合理的机制。

## 技术债务清单

### TD-01: AVB tonic 直注 → 感觉基线 + 突触通路
- **问题**: Step 12 直接向 AVB 注入 20pA tonic 电流，跳过了上游感觉→中间→命令的因果链
- **方案**: 12 个感觉神经元接收 15pA 自发基线活动 (REF: Bargmann 2006)，信号通过 AIY→AVB 和 RIB→AVB 突触传递
- **新增连接**: AIY→AVB (3 sections/side), RIB→AVB (2 sections/side)
- **结果**: V_AVB ≈ -39mV (release ~27%), 完全由突触驱动

### TD-02: SMD 正弦注入 → CCA-1 + 交叉抑制
- **问题**: Step 12 直接向 SMD 注入 0.8Hz 正弦电流，跳过了内在振荡机制
- **方案**: 
  - CCA-1 T-type Ca²⁺ 通道 (g_max=5.0 nS) 加入头部运动神经元 (SMD/RMD)
  - SMD dorsal↔ventral GABA 交叉抑制 (8 sections/pair) — 半中心振荡器
  - RMD dorsal↔ventral 交叉抑制 (6 sections/pair)
  - SMD→RMD 同侧兴奋 (3 sections)
- **CCA-1 参数**: m_half=-48mV, k=5; h_half=-55mV, k_h=-5, tau_h=80ms
- **状态**: 架构已建立，半中心振荡器产生强差分激活(mdiff~0.99)但尚未产生周期性振荡。需继续调优参数。

### TD-03: 本体感觉直注 → MEC 膜通道 ✅
- **问题**: Step 12 直接注入电流 I = gain × curvature，不经过离子通道
- **方案**: MechanoSensitiveChannel 类 (stretch-activated cation channel)
  - g_max=3.0 nS, E_rev=-10mV
  - 开放概率: `stretch² / (stretch² + K_half²)`, K_half=0.3
  - 加入 B 类运动神经元 (DB/VB) 的通道组
- **接口**: `SingleCompartmentNeuron::set_stretch_input(double stretch)` → 遍历通道找到 MEC 并设置拉伸值
- **REF**: Wen et al. 2012 - proprioceptive coupling within motor neurons

### TD-04: 简化速度公式 → 肌肉功率模型 ✅
- **问题**: Step 12 用 `wave_energy * 1.0` 简化公式，无物理基础
- **方案**: 肌肉功率模型
  - muscle_work = mean |dorsal - ventral| (肌肉差分做功)
  - wave_efficiency = 1 - exp(-curvature_variance × 100) (空间曲率变异 → 行波效率)
  - temporal_boost = 1 + min(dcurv/dt_rms × 10, 2) (曲率变化率 → 振荡加成)
  - speed = v_max × muscle_work × wave_eff × temporal_boost
  - v_max = 0.8 mm/s (补偿稀疏运动映射)
- **REF**: Fang-Yen et al. 2010, Boyle et al. 2012

## 额外修复

### Bug: set_muscle_activation 覆盖问题
- **原因**: 多个运动神经元映射到同一段时，后者用 `=` 覆盖前者
- **修复**: 改为 `std::max()` 取最大值

### Bug: DD/VD 抑制逻辑
- **原因**: DD/VD (GABAergic) 被当作兴奋性神经元处理，直接设置同侧激活
- **修复**: 添加 `is_inhibitory` 标志，DD 抑制腹侧, VD 抑制背侧。两遍扫描：先兴奋后抑制。

### Bug: 曲率时间步硬编码
- **原因**: `seg.curvature += dcurv * 0.001` 中 0.001 是硬编码值，与实际 dt 无关
- **修复**: 改为 `dcurv * dt`

### 离子通道噪声
- 添加 3pA 高斯噪声到所有神经元 (REF: White 1998, Faisal 2008)
- 生物学意义: 小神经元中通道随机开关产生显著热噪声

### 神经元兴奋性调优
- NCA 电导: 感觉 0.10, 中间 0.15, 运动 0.12 nS (原 0.03-0.05)
- 突触权重缩放: 0.1 → 0.3 nS/section
- 头部运动神经元: g_leak=1.0, E_leak=-60, 低 NCA(0.02) — 为 CCA-1 振荡优化

## 修改文件清单

| 文件 | 修改内容 |
|------|----------|
| `neuron/ion_channel.h` | 添加 MechanoSensitiveChannel; 调整 CCA-1 参数 |
| `neuron/single_compartment.h/cpp` | 添加 set_stretch_input(); 添加离子通道噪声 |
| `neuron/neuron_factory.h/cpp` | 添加 create_motor_b_class/create_motor_head; 调优 NCA |
| `connectome/connectome_loader.cpp` | 添加交叉抑制连接和 AIY/RIB→AVB 通路 |
| `connectome/connectome.h` | synapse_weight_scale 0.1→0.3 |
| `simulation/simulation_engine.h/cpp` | 移除占位符，添加生物学驱动方法 |
| `body/body_model.h/cpp` | set_muscle_activation 改 max; 添加 D-class 抑制; 肌肉功率模型 |
| `motor/motor_controller.h/cpp` | 添加 is_inhibitory; 两遍扫描 |
| `simulation/main.cpp` | 添加诊断输出 (mdiff, mcurv) |

## 验证结果

```
速度: 0.10-0.18 mm/s (生物学参考: 0.2-0.3 mm/s)
5秒位移: 0.64 mm (约 0.64 体长)
V_AVA: -31 ~ -35 mV (release ~60%)
V_AVB: -37 ~ -42 mV (release ~27%)
肌肉差分: mdiff 0.50-0.99 (背腹不对称)
曲率: mcurv 0.08-0.15 (身体弯曲)
数值稳定: 全程无发散
```

## 剩余技术债务

- **TD-02 未完全解决**: 半中心振荡器架构已建立但尚未产生周期性振荡。
  当前行为是 winner-take-all (一侧持续主导)，需进一步调优 CCA-1/SLO-1 参数
  使 winner 能通过 Ca²⁺ 适应机制回落，实现真正的背腹交替。
- **RFT 推力**: 当前用肌肉功率模型近似，当振荡工作后可恢复为严格 RFT。
