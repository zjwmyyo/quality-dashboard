@echo off
chcp 65001 >nul
title AI Passport QQ音乐播放器 - 后端启动
cd /d "%~dp0server"

echo ================================================
echo    AI Passport QQ音乐播放器 - 后端启动
echo ================================================

where docker >nul 2>nul
if %errorlevel%==0 (
  docker info >nul 2>nul
  if %errorlevel%==0 (
    echo 使用 Docker 启动...
    docker compose up -d --build
    timeout /t 3 >nul
    start http://localhost:3200
    echo 服务已后台启动，本窗口可关闭。停止: docker compose down
    pause
    exit /b 0
  ) else (
    echo 正在启动 Docker Desktop...
    start "" "C:\Program Files\Docker\Docker\Docker Desktop.exe"
    echo 请等待 Docker 启动后重新运行本脚本。
    pause
    exit /b 1
  )
)

where node >nul 2>nul
if %errorlevel%==0 (
  for /f "tokens=1 delims=." %%v in ('node -v') do set MAJOR=%%v
  setlocal enabledelayedexpansion
  set MAJOR=!MAJOR:v=!
  if !MAJOR! geq 20 (
    echo [1/2] 安装依赖...
    call npm install --no-audit --no-fund
    echo [2/2] 启动服务...
    timeout /t 1 >nul
    start http://localhost:3200
    node src\index.js
    exit /b 0
  )
)

echo 未检测到可用的 Docker 或 Node.js 20+。
echo 请安装其一后重试:
echo   Docker Desktop: https://www.docker.com/products/docker-desktop/
echo   Node.js 20+:    https://nodejs.org/
pause
