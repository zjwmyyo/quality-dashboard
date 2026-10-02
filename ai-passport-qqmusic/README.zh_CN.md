# AI Passport QQ 音乐播放器

[English](README.md)

本目录提供配套后端与可编译的 FoloToy AI Passport 播放器固件源码。公开固件首次开机会进入配网页面，不包含个人 Wi-Fi 信息或 QQ 音乐登录数据。

## 启动配套后端

在与 AI Passport 同一局域网、能够持续运行的电脑或小主机上执行：

```sh
cd server
docker compose up -d --build
```

Docker 会安装所需的音频转换程序。不使用 Docker 时，先安装 Node.js 20+ 和 ffmpeg，再在 `server` 目录执行 `npm install`、`npm start`。服务使用 3200 端口，只应在自己的局域网内使用；扫码登录与音频接口按单个家庭账号设计。

在电脑上打开 `http://localhost:3200`，使用 QQ 音乐 App 扫码确认，并记下电脑的局域网 IP 地址。

## 安装和配网

从 FoloToy 项目页安装合并固件。首次开机，屏幕显示 `Passport-Music-XXXX` 热点与这台设备专属的密码。手机连接该热点，浏览器打开 `http://192.168.4.1/`，填写 2.4 GHz Wi-Fi 信息和后端地址，例如 `http://192.168.1.100:3200`（换成电脑实际的局域网 IP）。保存后设备会重启。

若 Wi-Fi 连接失败，设置热点会重新出现。播放时电脑不可休眠，后端需持续运行。

当前每个歌单最多加载前 60 首。歌曲是否可播仍受 QQ 音乐账号权限和曲目可用性限制。本社区项目与 QQ 音乐无关联。

## 从源码编译

固件需要 ESP-IDF 5.5.3。在 `firmware` 目录执行：

```sh
idf.py build
idf.py merge-bin -o AI-Passport-QQMusic-full.bin
```

合并镜像从 `0x0` 写入，刷机可能清除设备设置。源码中的 FoloToy 板级文件沿用 MIT 许可，韩文字库使用随附的 Noto Sans CJK 许可。

不要提交 `server/data`、`firmware/sdkconfig`、构建产物、Wi-Fi 密码或 QQ 音乐登录凭证。
