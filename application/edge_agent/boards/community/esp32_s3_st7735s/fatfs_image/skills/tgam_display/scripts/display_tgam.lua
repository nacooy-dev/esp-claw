-- TGAM Display Module
-- 显示 TGAM1 脑电数据到 ST7735S 屏幕

local board_manager = require("board_manager")
local display = require("display")
local delay = require("delay")
local uart = require("uart")

local TAG = "[tgam_display]"

-- 状态
local s_running = false
local s_task_handle = nil
local s_panel_handle = nil
local s_io_handle = nil
local s_width = 0
local s_height = 0

-- 颜色定义
local COLOR_BG = { r = 10, g = 10, b = 20 }
local COLOR_TEXT = { r = 245, g = 244, b = 238 }
local COLOR_ATTENTION = { r = 72, g = 208, b = 235 }
local COLOR_MEDITATION = { r = 88, g = 210, b = 124 }
local COLOR_SIGNAL = { r = 255, g = 160, b = 60 }
local COLOR_GRID = { r = 40, g = 60, b = 80 }

-- 解析 TGAM1 数据包
-- 大包格式: AA AA 20 <32字节payload> CS
local function parse_tgam_packet(data)
    if #data < 35 then
        return nil
    end
    
    -- 查找帧头 AA AA
    local header_pos = 1
    while header_pos <= #data - 2 do
        if data[header_pos] == 0xAA and data[header_pos + 1] == 0xAA then
            break
        end
        header_pos = header_pos + 1
    end
    
    if header_pos > #data - 2 then
        return nil
    end
    
    -- 检查命令字节
    local cmd = data[header_pos + 2]
    local payload_len = data[header_pos + 3]
    
    if cmd ~= 0x80 then
        return nil
    end
    
    if payload_len ~= 0x20 then
        return nil
    end
    
    -- 提取 payload (32 字节)
    local payload = {}
    for i = 1, 32 do
        payload[i] = data[header_pos + 3 + i]
    end
    
    -- 解析指标
    local result = {
        signal = payload[1],           -- 信号质量 (0-255, 0=好)
        attention = payload[29],       -- 专注度 (0-100)
        meditation = payload[30],      -- 放松度 (0-100)
    }
    
    -- 解析 EEG 功率谱 (8 频段, 每频段 3 字节)
    result.eeg_power = {}
    for band = 1, 8 do
        local offset = (band - 1) * 3 + 2  -- 从 payload[2] 开始
        result.eeg_power[band] = (payload[offset] * 65536) + 
                                  (payload[offset + 1] * 256) + 
                                  payload[offset + 2]
    end
    
    return result
end

-- 绘制进度条
local function draw_progress_bar(x, y, width, height, value, max_value, color)
    local fill_width = math.floor(width * value / max_value)
    display.fill_rect(x, y, fill_width, height, color)
    display.draw_rect(x, y, width, height, COLOR_TEXT)
end

-- 绘制主界面
local function draw_main_screen(attention, meditation, signal)
    local w = s_width
    local h = s_height
    
    display.begin_frame({ clear = true, color = COLOR_BG })
    
    -- 标题
    display.draw_text_aligned(0, 8, w, 20, "TGAM Meditation", {
        color = COLOR_TEXT,
        font_size = 14,
        align = "center"
    })
    
    -- 专注度
    display.draw_text(8, 32, "ATT", { color = COLOR_ATTENTION, font_size = 12 })
    display.draw_text(w - 30, 32, tostring(attention), { color = COLOR_ATTENTION, font_size = 12 })
    draw_progress_bar(8, 48, w - 16, 12, attention, 100, COLOR_ATTENTION)
    
    -- 放松度
    display.draw_text(8, 68, "MED", { color = COLOR_MEDITATION, font_size = 12 })
    display.draw_text(w - 30, 68, tostring(meditation), { color = COLOR_MEDITATION, font_size = 12 })
    draw_progress_bar(8, 84, w - 16, 12, meditation, 100, COLOR_MEDITATION)
    
    -- 信号质量
    local signal_quality = math.max(0, 100 - signal)  -- 反转：值越小越好
    display.draw_text(8, 104, "SIG", { color = COLOR_SIGNAL, font_size = 12 })
    display.draw_text(w - 30, 104, tostring(signal_quality), { color = COLOR_SIGNAL, font_size = 12 })
    draw_progress_bar(8, 120, w - 16, 12, signal_quality, 100, COLOR_SIGNAL)
    
    -- 底部提示
    display.draw_text_aligned(0, h - 16, w, 12, "Ready", {
        color = COLOR_GRID,
        font_size = 10,
        align = "center"
    })
    
    display.present()
    display.end_frame()
end

-- 主循环任务
local function tgam_display_task()
    local attention = 0
    local meditation = 0
    local signal = 0
    local last_refresh = 0
    
    while s_running do
        -- 读取 UART 数据
        local data = uart.read_bytes(1, 35)
        if data and #data > 0 then
            local packet = parse_tgam_packet(data)
            if packet then
                attention = packet.attention or 0
                meditation = packet.meditation or 0
                signal = packet.signal or 0
            end
        end
        
        -- 每 200ms 刷新一次屏幕
        local now = os.time()
        if now - last_refresh >= 0.2 then
            draw_main_screen(attention, meditation, signal)
            last_refresh = now
        end
        
        delay.delay_ms(50)
    end
end

-- 启动显示
local function start()
    if s_running then
        print(TAG .. " Already running")
        return
    end
    
    -- 获取显示参数
    local panel_handle, io_handle, width, height, panel_if = 
        board_manager.get_display_lcd_params("display_lcd")
    
    if not panel_handle then
        print(TAG .. " ERROR: Failed to get display params")
        return
    end
    
    s_panel_handle = panel_handle
    s_io_handle = io_handle
    s_width = width
    s_height = height
    
    print(TAG .. " Display: " .. width .. "x" .. height)
    
    -- 初始化显示
    local ok, err = pcall(display.init, panel_handle, io_handle, width, height, panel_if)
    if not ok then
        print(TAG .. " ERROR: display.init failed: " .. tostring(err))
        return
    end
    
    -- 初始化 UART1 (TGAM1)
    uart.init(1, {
        rx_gpio = 18,
        tx_gpio = 17,
        baudrate = 9600,
        data_bits = 8,
        parity = "NONE",
        stop_bits = 1,
    })
    
    print(TAG .. " TGAM1 UART1 initialized (GPIO17/18, 9600 baud)")
    
    -- 启动显示任务
    s_running = true
    s_task_handle = task.spawn(tgam_display_task)
    
    print(TAG .. " Started")
end

-- 停止显示
local function stop()
    if not s_running then
        return
    end
    
    s_running = false
    
    if s_task_handle then
        task.kill(s_task_handle)
        s_task_handle = nil
    end
    
    -- 清理显示
    pcall(display.end_frame)
    pcall(display.deinit)
    
    -- 关闭 UART
    uart.deinit(1)
    
    print(TAG .. " Stopped")
end

-- 手动刷新一次
local function refresh()
    local data = uart.read_bytes(1, 35)
    local attention = 0
    local meditation = 0
    local signal = 0
    
    if data and #data > 0 then
        local packet = parse_tgam_packet(data)
        if packet then
            attention = packet.attention or 0
            meditation = packet.meditation or 0
            signal = packet.signal or 0
        end
    end
    
    draw_main_screen(attention, meditation, signal)
end

-- 模块导出
local display_tgam = {
    start = start,
    stop = stop,
    refresh = refresh,
    parse_packet = parse_tgam_packet,
}

return display_tgam
