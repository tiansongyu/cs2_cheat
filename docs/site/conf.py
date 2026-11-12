"""Sphinx configuration for the project's GitHub Pages documentation."""
import os

project = "CS2 External ESP"
author = "CS2 External ESP contributors"
copyright = "CS2 External ESP contributors"
language = "zh_CN"
extensions = ["myst_parser", "sphinx_rtd_theme", "sphinx.ext.githubpages"]
source_suffix = {".md": "markdown", ".rst": "restructuredtext"}
root_doc = "index"
exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]
myst_heading_anchors = 3
html_theme = "sphinx_rtd_theme"
html_title = "CS2 External ESP 项目文档"
html_static_path = ["_static"]
html_css_files = ["custom.css"]
html_theme_options = {"navigation_depth": 3, "collapse_navigation": False}
html_baseurl = os.environ.get("DOCS_BASE_URL", "https://tiansongyu.github.io/cs2_cheat/")
html_show_sourcelink = True
html_search_language = "zh"
