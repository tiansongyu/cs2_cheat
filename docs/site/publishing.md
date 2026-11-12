# 文档维护与发布

本站使用 Sphinx、MyST Markdown 和 Read the Docs 开发的 `sphinx_rtd_theme`。源文件位于 `docs/site/`，现有 `docs/*.md` 学习文章继续保留原路径。

## 安装与本地预览

需要 Python 3.10 或更新的兼容版本；CI 使用 Python 3.12。在仓库根目录执行：

```bash
python3 -m venv .venv-docs
source .venv-docs/bin/activate
python -m pip install -r docs/requirements.txt
python -m sphinx -W --keep-going -b html docs/site docs/_build/html
python -m http.server 8000 --directory docs/_build/html
```

Windows PowerShell 可用 `py -3 -m venv .venv-docs` 创建环境，并通过 `.\.venv-docs\Scripts\Activate.ps1` 激活；其余 `python -m ...` 命令相同。

打开 `http://localhost:8000/`，检查目录、图片、搜索和手机宽度下的导航。`-W` 会把构建警告作为失败处理。输出目录含 `.nojekyll`，由 `sphinx.ext.githubpages` 生成。

## GitHub Pages

默认项目地址为 `https://tiansongyu.github.io/cs2_cheat/`。实际发布状态以仓库 Pages 设置和 Actions 运行结果为准。

1. 在仓库 **Settings → Pages → Build and deployment** 中把 Source 设置为 **GitHub Actions**。
2. 将文档及 `.github/workflows/docs.yml` 提交到仓库。推送到 `main` 或文档发布分支 `docs/sphinx-pages` 后，相关文件变更自动构建并发布。
3. 在 Pull Request 中只构建检查，不执行部署。
4. 如需发布指定分支，在 **Actions → Documentation → Run workflow** 中选择该分支手动运行。首次使用时，工作流文件须已存在于默认分支。
5. 等待 build 和 deploy 两个任务成功，从部署环境的 URL 打开网站。

手动发布非默认分支时，`github-pages` 环境的分支规则也必须允许该分支。不要为绕过现有仓库保护规则随意放宽环境设置。

工作流只上传 `docs/_build/html`，不会上传源码树、录制文件或 Relay 配置。构建任务仅需读取仓库；部署任务使用 `pages: write` 和 `id-token: write`。

## 修改内容

新增 Markdown 页面后，将文件名加入 `index.md` 的 `toctree`。站内链接使用相对路径，例如 `[构建](build.md)`；代码与命令使用语言明确的 fenced code block。

更新项目说明时同时核对源码、配置和测试。功能设计、代码实现、自动测试和实机验收是不同状态，尤其不能把皮肤参数读回写成已完成材质验证。

依赖版本固定在 `docs/requirements.txt`。升级后重新执行严格构建并检查页面。`custom.css` 只补充中文字体、阅读宽度和键盘焦点，保留 Read the Docs 主题导航与布局。

## 官方参考

- [Read the Docs 主题安装说明](https://sphinx-rtd-theme.readthedocs.io/en/stable/installing.html)
- [GitHub Pages 自定义工作流](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages)
