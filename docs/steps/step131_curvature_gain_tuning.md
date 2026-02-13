# Step 131: 曲率增益调优 + 可视化升级

## 动机

用户指出虫体"基本上一直是直的"，对比 OpenWorm Sibernetic 的弯曲效果差距明显。
截图诊断：曲率仅 0.023/mm，生物学应为 3-5/mm。

## 根因分析

信号链追踪：
1. **SMD 半中心振荡正常** — SMDDL: -79~-68mV, SMDVL: -65~-20mV, 差值 13mV ✅
2. **motor_controller** — release rate → set_muscle_activation(seg, dorsal, release) ✅
3. **compute_curvatures** — target = muscle_gain_ × (dorsal - ventral) ← **瓶颈**
4. **muscle_gain_ = 0.3** — 即使差值为 1.0，最大曲率仅 0.3/mm ❌

附加因素：头部 10 个运动神经元 (SMD×4 + URA×4 + SAA×4 + SIA×4 + SIB×4) 用 MAX 聚合，
导致背腹双高（共收缩），差值进一步缩小。

## 修复

- `muscle_gain_`: 0.3 → **8.0** (26.7×)
- 效果：差值 ~0.5 → 曲率 ~4/mm（生物学范围）
- regtest 基线更新：curvature 0.06→1.1, midbody 0.20→3.0

## 可视化升级

参考 OpenWorm Sibernetic 风格重写渲染器：
- **伪 3D 管状体** — 5 条纵向条带，抛物线光照衰减模拟圆柱形
- **Sibernetic 配色** — 肌肉激活=红/橙，松弛=米色/灰
- **高光带** — 中心线偏移高光模拟镜面反射
- **曲率波图** — 填充面积+曲线叠加，暖色=背弯，冷色=腹弯
- **椭球渐缩** — 96% 渐缩因子

## 验证
- regtest: 20/20 通过
- 曲率幅度: 头部 1.1/mm, 体中 3.0/mm (生物学范围 3-5/mm)
- 速度: 0.2 mm/s (不变)

## 修改文件
- `src/body/body_model.h`: muscle_gain_ 0.3 → 8.0
- `src/simulation/regression_test.cpp`: 曲率基线更新
- `src/visualization/worm_renderer_3d.cpp`: 伪3D渲染器重写
