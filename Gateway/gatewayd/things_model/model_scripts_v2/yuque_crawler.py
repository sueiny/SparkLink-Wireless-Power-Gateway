#!/usr/bin/env python3
"""
语雀文档爬虫

功能：
1. 登录语雀
2. 获取文档目录
3. 下载文档内容

使用方法：
python3 yuque_crawler.py
"""

import requests
import json
import re
from bs4 import BeautifulSoup
import os


class YuqueCrawler:
    """语雀爬虫"""
    
    def __init__(self, username: str, password: str):
        self.username = username
        self.password = password
        self.session = requests.Session()
        self.session.headers.update({
            'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36',
            'Referer': 'https://yunteng.yuque.com'
        })
        self.base_url = 'https://yunteng.yuque.com'
        self.logged_in = False
    
    def login(self) -> bool:
        """登录语雀"""
        print("[1] 获取登录页面...")
        
        # 获取登录页面
        login_page = self.session.get(f"{self.base_url}/login")
        
        # 提取CSRF token
        soup = BeautifulSoup(login_page.text, 'html.parser')
        csrf_token = None
        meta_tag = soup.find('meta', {'name': 'csrf-token'})
        if meta_tag:
            csrf_token = meta_tag.get('content')
        
        if not csrf_token:
            # 尝试从cookie获取
            csrf_token = self.session.cookies.get('_csrf_token')
        
        print(f"  CSRF Token: {csrf_token[:20] if csrf_token else 'None'}...")
        
        # 提交登录
        print("[2] 提交登录...")
        login_data = {
            'login': self.username,
            'password': self.password,
            '_csrf_token': csrf_token or ''
        }
        
        headers = {
            'Content-Type': 'application/json',
            'X-CSRF-Token': csrf_token or '',
            'Referer': f'{self.base_url}/login'
        }
        
        resp = self.session.post(
            f"{self.base_url}/api/accounts/login",
            json=login_data,
            headers=headers
        )
        
        print(f"  响应状态: {resp.status_code}")
        
        if resp.status_code == 200:
            result = resp.json()
            if result.get('ok') or result.get('token'):
                self.logged_in = True
                print("[OK] 登录成功!")
                return True
        
        # 尝试其他登录方式
        print("[3] 尝试其他登录方式...")
        resp2 = self.session.post(
            f"{self.base_url}/api/m/accounts/login",
            json=login_data,
            headers=headers
        )
        
        if resp2.status_code == 200:
            result = resp2.json()
            if result.get('ok') or result.get('token'):
                self.logged_in = True
                print("[OK] 登录成功!")
                return True
        
        print(f"[ERROR] 登录失败: {resp.text[:200]}")
        return False
    
    def get_docs(self, group: str, book: str) -> list:
        """获取文档列表"""
        print(f"\n获取文档列表: {group}/{book}")
        
        url = f"{self.base_url}/api/catalog_nodes"
        params = {
            'book_id': f"{group}/{book}"
        }
        
        resp = self.session.get(url, params=params)
        if resp.status_code == 200:
            data = resp.json()
            return data.get('data', [])
        
        print(f"[ERROR] 获取失败: {resp.status_code}")
        return []
    
    def get_doc_content(self, group: str, book: str, slug: str) -> str:
        """获取文档内容"""
        url = f"{self.base_url}/{group}/{book}/{slug}"
        
        resp = self.session.get(url)
        if resp.status_code == 200:
            soup = BeautifulSoup(resp.text, 'html.parser')
            
            # 提取文档内容
            content_div = soup.find('div', {'class': 'yuque-doc-content'})
            if not content_div:
                content_div = soup.find('article')
            if not content_div:
                content_div = soup.find('div', {'id': 'content'})
            
            if content_div:
                return content_div.get_text(strip=True, separator='\n')
            
            # 返回页面文本
            return soup.get_text(strip=True, separator='\n')[:5000]
        
        return ""
    
    def crawl_book(self, group: str, book: str, output_dir: str):
        """爬取整本书"""
        os.makedirs(output_dir, exist_ok=True)
        
        docs = self.get_docs(group, book)
        print(f"找到 {len(docs)} 个文档")
        
        for doc in docs:
            title = doc.get('title', 'untitled')
            slug = doc.get('slug', '')
            
            if not slug:
                continue
            
            print(f"下载: {title}")
            content = self.get_doc_content(group, book, slug)
            
            if content:
                filename = f"{title}.md"
                filepath = os.path.join(output_dir, filename)
                with open(filepath, 'w', encoding='utf-8') as f:
                    f.write(f"# {title}\n\n{content}")
                print(f"  保存: {filepath}")


def main():
    """主函数"""
    print("="*60)
    print("语雀文档爬虫")
    print("="*60)
    
    username = "18476971193"
    password = "h11081108h"
    
    crawler = YuqueCrawler(username, password)
    
    # 登录
    if not crawler.login():
        print("\n[ERROR] 登录失败，请检查账号密码")
        print("\n提示：语雀可能需要手机验证码或扫码登录")
        print("建议：手动登录后复制文档内容")
        return
    
    # 获取文档
    output_dir = "/home/sueiny/rk3506_linux6.1_v1.2.0/app/Gateway/docs/thingskit-yuque"
    crawler.crawl_book("avshoi", "v1xdocs", output_dir)


if __name__ == '__main__':
    main()
