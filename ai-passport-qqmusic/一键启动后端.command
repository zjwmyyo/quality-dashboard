#!/bin/bash
# 一键启动 QQ 音乐后端（macOS 双击运行）
# 优先使用 Docker；没有 Docker 时使用本机 Node.js（>=20）。
cd "$(dirname "$0")/server" || exit 1

echo "================================================"
echo "   AI Passport QQ 音乐播放器 - 后端启动"
echo "================================================"

start_with_node() {
  echo "[1/2] 安装依赖（首次运行需要联网）..."
  npm install --no-audit --no-fund || { echo "npm install 失败"; exit 1; }
  echo "[2/2] 启动服务..."
  sleep 1
  open "http://localhost:3200" 2>/dev/null
  exec node src/index.js
}

if command -v docker >/dev/null 2>&1; then
  if ! docker info >/dev/null 2>&1; then
    echo "检测到 Docker 但未运行，正在启动 Docker Desktop..."
    open -a Docker 2>/dev/null
    for i in $(seq 1 60); do docker info >/dev/null 2>&1 && break; sleep 2; done
  fi
  if docker info >/dev/null 2>&1; then
    echo "使用 Docker 启动（后台常驻，开机可用 docker compose logs 查看日志）..."
    docker compose up -d --build || { echo "Docker 启动失败，改用 Node..."; start_with_node; }
    sleep 3
    open "http://localhost:3200" 2>/dev/null
    echo ""
    echo "服务已在后台启动。浏览器应已自动打开扫码页。"
    echo "停止服务： cd server && docker compose down"
    echo "本窗口可以关闭。"
    exit 0
  fi
fi

if command -v node >/dev/null 2>&1; then
  NODE_MAJOR=$(node -v | sed 's/v\([0-9]*\).*/\1/')
  if [ "$NODE_MAJOR" -ge 20 ] 2>/dev/null; then
    start_with_node
  else
    echo "Node.js 版本过低（需要 20+），请升级：https://nodejs.org/"
    exit 1
  fi
else
  echo "未检测到 Docker 或 Node.js。"
  echo "请任选其一安装后重试："
  echo "  Docker Desktop: https://www.docker.com/products/docker-desktop/"
  echo "  Node.js 20+:    https://nodejs.org/"
  exit 1
fi
