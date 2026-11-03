# 🏎️ STM32 F1 Reaction Time Tester

## 專題介紹

本專題使用 **STM32 NUCLEO-G474RE** 開發板，製作模擬 F1 賽車起跑燈號的反應速度測試系統。

透過三顆紅色 LED 模擬起跑燈，使用者必須在紅燈全部熄滅後按下反應按鈕，系統會記錄反應時間，並計算五次有效成績的平均值。

## 開發環境

- 開發板：NUCLEO-G474RE
- 開發工具：STM32CubeIDE
- 程式語言：C
- 使用函式庫：STM32 HAL Library

## GPIO 腳位配置

| 元件 | 腳位 | 功能 |
|---|---|---|
| 紅燈 1 | PC8 | GPIO Output |
| 紅燈 2 | PC6 | GPIO Output |
| 紅燈 3 | PC5 | GPIO Output |
| 綠燈 | PC9 | TIM3_CH4 PWM |
| 黃燈 | PA9 | TIM1_CH2 PWM |
| 開始按鈕 | PC13 | EXTI 中斷 |
| 反應按鈕 | PA8 | EXTI 中斷 |
| UART TX | PA2 | LPUART1_TX |
| UART RX | PA3 | LPUART1_RX |

## 系統功能

1. 按下開始按鈕，三顆紅燈每隔 500 ms 依序亮起。
2. 三顆紅燈全亮後，隨機等待 1～3 秒。
3. 紅燈全部熄滅，系統開始計時。
4. 使用者按下反應按鈕，停止計時。
5. 反應時間在 2000 ms 內，綠燈 PWM 閃爍。
6. 搶跑或超過 2000 ms，黃燈 PWM 閃爍。
7. 記錄五次有效反應時間。
8. 計算五次平均反應時間。
9. 透過 UART 將測試結果傳送至電腦。

## 使用方式

1. 使用 STM32CubeIDE 開啟專案。
2. 編譯並燒錄程式至 NUCLEO-G474RE。
3. 按下 PC13 開始按鈕。
4. 等待三顆紅燈全部熄滅。
5. 按下 PA8 反應按鈕。
6. 查看測試結果。
7. 重複測試直到完成五次有效成績。

## UART 成績顯示範例

```text
Test 1/5: 0.245 s
Test 2/5: 0.310 s
Test 3/5: 0.198 s
Test 4/5: 0.275 s
Test 5/5: 0.220 s

Average: 249.6 ms
```

## 使用技術

- GPIO：控制紅色 LED
- EXTI：偵測開始與反應按鈕
- PWM：控制綠燈及黃燈閃爍
- HAL_GetTick：毫秒反應時間測量
- UART：傳送反應成績至電腦
- Array：儲存五次有效成績

## 專題狀態

開發與測試中。
