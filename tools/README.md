# ruyijs 构建工具

## 组成与来源

- **JavaScript 引擎**：`js.exe` 的引擎是 Firefox 官方的 SpiderMonkey，来自 Firefox 官方源码
  （标签 `FIREFOX_159_0_RELEASE`）。`tools/build.py` 把 Firefox 编出的 SpiderMonkey 对象，
  与本仓库 `src/` 下的 ruyijs 浏览器环境代码一起链接成 `js.exe`。
- **网络请求库**：使用 Firefox 官方的 Necko 网络栈（Firefox 159 的 HTTP/1.1、HTTP/3、
  brotli 原生组件）。它随运行时包发布，位于 `vendor/necko-derived`。
  Windows 运行时包里的 Necko 按上面这条发布。macOS 的请求走 donor 编出的 `xpcshell`，
  已验证的是 HTTP/1.1 和 HTTP/2，步骤见 [docs/macos.zh-CN.md](../docs/macos.zh-CN.md)。
- **第三方原生库**：`vendor/compression-native`（zlib-rs、brotli）、`vendor/image-native`
  （dav1d、libavif、libyuv、qcms）和 `vendor/urlpattern-native` 都取自 Firefox 官方源码树，
  各目录保留原许可证，`manifest.json` 记录每个文件的 SHA-256。

引擎和 Necko 都可以从 Firefox 官方源码自行编译。

## 不想编译

直接使用 GitHub Release 中的预编译运行时：

- `ruyijs-runtime-<版本>-win-amd64.zip`：完整运行时（`dist/js.exe`、运行库、Necko）。
  解压后设置 `RUYIJS_SHELL=<解压目录>\dist\js.exe`，或把它放在源码根目录（得到
  `dist/js.exe` 和 `vendor/necko-derived`），`python/ruyijs` 会自动找到。
- `pip install ruyijs`：wheel 已内置同一运行时。

## 自行编译 js.exe

需要 Windows x64。

1. 获取 Firefox 官方源码并切到 158 正式版：

   ```powershell
   git clone https://github.com/mozilla-firefox/firefox
   cd firefox
   git checkout FIREFOX_158_0_RELEASE
   ```

2. 用 `./mach bootstrap` 安装 Firefox 的工具链（clang-cl、lld-link、Rust、VS/Windows SDK
   会放在 `%USERPROFILE%\.mozbuild`）。

3. 编译 SpiderMonkey standalone。创建 `mozconfig-js`：

   ```
   mk_add_options MOZ_OBJDIR=@TOPSRCDIR@/obj-js-standalone
   ac_add_options --enable-project=js
   ac_add_options --enable-optimize
   ac_add_options --disable-debug
   ac_add_options --disable-tests
   ac_add_options --enable-rust-simd
   ac_add_options --target=x86_64-pc-windows-msvc
   ```

   然后执行：

   ```powershell
   $env:MOZCONFIG = "$PWD\mozconfig-js"
   ./mach build
   ```

4. 在 ruyijs 根目录构建：

   ```powershell
   $env:RUYIJS_DONOR_OBJ = "<firefox>\obj-js-standalone"
   python -B tools/build.py `
     --toolchain-bin "$env:USERPROFILE\.mozbuild\clang\bin" `
     --vs-root "$env:USERPROFILE\.mozbuild\vs"
   ```

   输出为 `dist/js.exe`；构建记录（输入哈希、命令、工具版本）写入
   `artifacts/build-runtime.json`。只改 `src/` 后再次运行即为增量编译。

5. 运行时还需要 `dist/` 里的运行库和 `vendor/necko-derived`。最简单的做法：
   解压 Release 中的运行时包，再用新编译的 `dist/js.exe` 覆盖其中的同名文件。

### 可选输入

`--firefox-font-prefs`（`dist/bin/greprefs.js`）和 `--firefox-bindings`（`dom/bindings`）
来自完整的 Firefox **浏览器**构建。不提供时，使用 `tools/generated/` 中已从 Firefox 159
生成好的 `firefox_font_prefs.h` 和 `firefox_binding_order.h`，所以只需编译 SpiderMonkey。

所有本机路径都通过参数或环境变量传入：

| 参数 | 环境变量 | 说明 |
| --- | --- | --- |
| `--donor-obj` | `RUYIJS_DONOR_OBJ` | SpiderMonkey standalone 的 obj 目录（必需） |
| `--toolchain-bin` | | LLVM `bin` 目录（clang-cl、lld-link） |
| `--vs-root` | | 含 VC 与 Windows Kits 的 VS 目录 |
| `--runtime-dll-dir` | | 匹配的 MSVC CRT redistributable |
| `--firefox-font-prefs` | `RUYIJS_FIREFOX_FONT_PREFS` | 可选，见上 |
| `--firefox-bindings` | `RUYIJS_FIREFOX_BINDINGS` | 可选，见上 |

其他参数：`--prepare-only` 只准备输入；`--no-incremental` 全量重编；`--jobs N` 编译进程数。

## macOS arm64

macOS 不使用上面的 Windows 预编译运行时。外壳从源码构建，Firefox donor 放在仓库外面。
步骤、已验证的 HTTP/1.1 与 HTTP/2，以及仍然留空的表面写在
[docs/macos.zh-CN.md](../docs/macos.zh-CN.md)。

在仓库根目录、且已设置 `MACOSX_DEPLOYMENT_TARGET=26.0` 时：

```sh
python3.12 -B tools/bootstrap_macos_helper.py
python3.12 -B tools/build.py --donor-obj ../firefox-159/obj-js-standalone
```

`bootstrap_macos_helper.py` 只在 macOS 上创建 `vendor/necko-derived/python`。
已经有 `pyvenv.cfg` 时它原样退出，不重装。不要把 `RUYIJS_DONOR_OBJ` 写进长期使用的 shell 配置。

## 文件

| 文件 | 职责 |
| --- | --- |
| `build.py` | 构建编排、输入身份和产物版本检查 |
| `bootstrap_macos_helper.py` | 在 macOS 上创建 Necko helper 虚拟环境；已存在则保持不动 |
| `donor.py` | 读取 donor 配置、对象列表和库路径 |
| `generate.py` | 生成保留原许可头的 shell 单元和内嵌资源 |
| `native.py` | 调用编译器/链接器，部署运行库 |
| `client.py` | 构建 C ABI DLL、导入库和公开头文件 |
| `geometry_fonts.py` | 字体配置和 HarfBuzz 编译输入 |
| `binding_order.py` | Firefox WebIDL 原型成员顺序表 |
| `sanitizer_data.py` | HTML Sanitizer 默认配置数据 |
| `product_sources.py` | 生产源码清单、环境基线 profile 和领域编译边界 |
| `compression_native.py` / `image_native.py` / `urlpattern_native.py` | 第三方原生库编译 |
| `provenance.py` | SHA-256 与输入身份复验 |
| `wheel_runtime.py` | `setup.py` 打包 wheel 时下载并校验 `runtime.json` 固定的运行时 |
| `generated/` | 从 Firefox 159 浏览器构建生成的两个头文件 |
