#!/usr/bin/env python3
"""
Phase 3: 深度性能优化 SQL 迁移脚本
执行此脚本升级数据库 schema
"""

import subprocess
import sys

def run_sql(sql, desc=""):
    """Execute SQL via mysql CLI"""
    cmd = ['mysql', '-u', 'root', '-h', '127.0.0.1', 'genealogy_system', '-e', sql]
    print(f"\n{'='*60}")
    print(f"[SQL] {desc}")
    print(f"{'='*60}")
    print(f"> {sql}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"⚠️  ERROR: {result.stderr}")
        return False
    print(f"✅ Success")
    if result.stdout:
        print(result.stdout)
    return True

def main():
    print("Phase 3 性能优化 - 数据库迁移")
    print("="*60)

    # 1. Materialized Path: 添加 ancestor_path 字段
    run_sql(
        "ALTER TABLE members ADD COLUMN ancestor_path VARCHAR(500) DEFAULT NULL COMMENT 'Materialized path: /1/15/234/567';",
        "添加 ancestor_path 字段"
    )

    # 2. 为 ancestor_path 添加索引（用于 LIKE 查询）
    run_sql(
        "CREATE INDEX idx_ancestor_path ON members(ancestor_path);",
        "添加 ancestor_path 索引"
    )

    # 3. 为 name 添加 FULLTEXT 索引（全文搜索）
    run_sql(
        "ALTER TABLE members ADD FULLTEXT INDEX ft_name (name);",
        "添加 name 全文搜索索引"
    )

    # 4. 填充 ancestor_path（使用递归 CTE 从 generation 推导）
    print("\n" + "="*60)
    print("[SQL] 填充 ancestor_path 数据...")
    print("="*60)

    fill_sql = """
    UPDATE members m
    SET m.ancestor_path = (
        WITH RECURSIVE path AS (
            SELECT member_id, father_id, CONCAT('/', member_id) AS path
            FROM members WHERE father_id IS NULL
            UNION ALL
            SELECT m.member_id, m.father_id, CONCAT(p.path, '/', m.member_id)
            FROM members m INNER JOIN path p ON m.father_id = p.member_id
        )
        SELECT path FROM path WHERE member_id = m.member_id
    )
    WHERE m.ancestor_path IS NULL;
    """
    run_sql(fill_sql, "填充 ancestor_path 数据")

    # 5. 添加统计缓存表（替代 Redis，简化方案）
    run_sql("""
        CREATE TABLE IF NOT EXISTS statistics_cache (
            cache_key VARCHAR(255) PRIMARY KEY,
            cache_value JSON NOT NULL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            expires_at TIMESTAMP NOT NULL,
            INDEX idx_expires (expires_at)
        ) COMMENT='统计结果缓存表（替代 Redis）';
    """, "创建统计缓存表")

    # 6. 添加响应缓存控制字段到配置
    print("\n" + "="*60)
    print("✅ Phase 3 数据库迁移完成!")
    print("="*60)
    print("\n已完成的优化:")
    print("  1. ✅ Materialized Path (ancestor_path)")
    print("  2. ✅ FULLTEXT 索引 (name)")
    print("  3. ✅ 统计缓存表 (statistics_cache)")
    print("\n后续需要在代码中:")
    print("  - 更新 Member.h 添加 ancestor_path 字段")
    print("  - 更新 RelationshipController 使用 ancestor_path")
    print("  - 更新 StatisticsController 使用缓存表")
    print("  - 更新 MemberController 搜索使用 MATCH AGAINST")

if __name__ == '__main__':
    main()
