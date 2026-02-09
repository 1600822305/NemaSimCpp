# Step 7-11: 身体模型 + 运动控制 + 环境 + 仿真引擎

> 日期: 2026-02-10
> 状态: ✅ 完成

---

## Step 7: 2D 弹性杆身体模型

### 模型

48 段弹性杆，每段包含：位置 (Vector2d)、朝向角、曲率、背/腹侧肌肉激活度。

### 物理参数

| 参数 | 值 | 说明 |
|------|---|------|
| 体长 | 1.0 mm | 成体 ~1-1.5 mm |
| 体半径 | 0.04 mm (40 μm) | |
| 段长 | 1/48 mm ≈ 21 μm | |
| 刚度 | 10 nN·mm² | 弯曲恢复力 |
| 阻尼 | 0.5 | 曲率变化阻尼 |
| 肌肉增益 | 0.3 | 最大曲率/激活度 |
| 法向阻力 | 10× 切向 | 低 Re 各向异性 (Re ~ 10⁻²) |

### 运动机制

1. 差异激活 → 目标曲率: `κ_target = gain × (dorsal - ventral)`
2. 弹簧驱动: `dκ/dt = stiffness × (κ_target - κ) - damping × κ`
3. 角度传播: `θ[i] = θ[i-1] - κ[i] × segment_length`
4. 前进推进: 波能量 → 前进速度 (简化阻力力理论)

### 本体感觉接口

```cpp
double get_local_curvature(int segment);  // 供 DVA/PVD 等本体感觉神经元
double get_local_stretch(int segment);    // 供 B 类运动神经元拉伸受体
```

---

## Step 8: 运动控制器

22 个运动神经元到肌肉段的映射：

| 神经元 | 段范围 | 侧 | 功能 |
|--------|-------|-----|------|
| DB01-03 | 4-10/10-20/20-30 | 背侧 | 前进背侧 |
| VB01-03 | 4-10/10-20/20-30 | 腹侧 | 前进腹侧 |
| DA01-03 | 4-10/10-20/20-30 | 背侧 | 后退背侧 |
| VA01-03 | 4-10/10-20/20-30 | 腹侧 | 后退腹侧 |
| DD01-03 | 4-10/10-20/20-30 | 背侧 | 交叉抑制 |
| VD01-03 | 4-10/10-20/20-30 | 腹侧 | 交叉抑制 |
| SMDDL/DR | 0-4 | 背侧 | 头部运动 |
| SMDVL/VR | 0-4 | 腹侧 | 头部运动 |

工作流: 读取运动神经元 `get_transmitter_release_rate()` → 设置对应肌肉段 `set_muscle_activation()`。

---

## Step 9: 环境与化学梯度场

### 竞技场

50×50 mm 2D 空间，100×100 网格 (dx=dy=0.5 mm)。

### ChemicalField

- **高斯点源**: `C(r) = strength × exp(-r²/(2σ²))`, σ²=25 mm²
- **扩散**: 显式有限差分, `D = 0.001 mm²/s`
- **采样**: 双线性插值
- **稳定性**: `rx + ry < 0.5` 检查，超过则跳过

### 默认环境

食物源 (吸引物) 放置在 (35, 35)，线虫起始于 (25, 25)。

---

## Step 10: 仿真引擎

### 主循环 (每步)

```
1. environment_.step(dt)                      // 化学扩散
2. sensors_.sample_environment(env, body)      // 感觉采样 (占位)
3. connectome_.compute_synaptic_currents(neurons)  // 突触电流
4. for neuron : neurons → neuron->step(dt)    // 膜电位更新
5. motor_controller_.update(neurons, body)    // 运动输出
6. body_.update_physics(dt)                   // 身体物理
7. step_callback_(...)                        // 回调 (记录/可视化)
```

### 初始化

- 默认连接组: `ConnectomeLoader::generate_default_connectome()`
- NeuronFactory 按类型创建 58 个神经元
- 身体初始化于竞技场中心 (25, 25)，朝向 0 rad (右)

---

## Step 11: 首次运行验证

### 运行结果

```
=== C. elegans Neural Simulation ===
Neurons: 58
Chemical synapses: 54
Gap junctions: 6
Duration: 5000 ms
Time step: 0.5 ms

[t=500.0 ms]  V_AVAL=-51.7 V_AVBL=-52.8  speed=0.0000
[t=1000.0 ms] V_AVAL=-51.7 V_AVBL=-52.8  speed=0.0000
...
Simulation complete. Final time: 5000 ms
```

### 验证项

| 检查项 | 结果 | 说明 |
|--------|------|------|
| 编译 | ✅ 零错误 | 2 个 Unicode 警告 |
| 数值稳定 | ✅ | 无 NaN/Inf，5s 仿真无发散 |
| 膜电位范围 | ✅ | -51 ~ -53 mV (合理静息态) |
| 网络平衡 | ✅ | 无外部输入时收敛到稳态 |
| 输出文件 | ✅ | trajectory.csv 正常生成 |
| 运行时间 | ✅ | < 1 秒完成 10000 步 |
| 线虫运动 | ❌ 预期 | 无驱动输入，speed=0 正确 |

### 下一步

需要添加 tonic 驱动或感觉输入才能让线虫运动 → Step 12。
