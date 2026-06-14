-- Phase 3: 深度性能优化迁移脚本
-- 适用于已有数据库的增量升级（不会影响现有数据）
-- 执行方式: mysql -u root genealogy_db < migration_phase3.sql

-- ============================================================
-- 1. Materialized Path: 添加 ancestor_path 字段
-- ============================================================

-- 添加字段（如果不存在）
SET @column_exists = (
    SELECT COUNT(*) FROM INFORMATION_SCHEMA.COLUMNS
    WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'Member' AND COLUMN_NAME = 'ancestor_path'
);
SET @sql = IF(@column_exists = 0,
    'ALTER TABLE Member ADD COLUMN ancestor_path VARCHAR(500) DEFAULT NULL COMMENT ''Materialized path: /1/15/234/567''',
    'SELECT ''ancestor_path column already exists'''
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- 添加索引（如果不存在）
SET @index_exists = (
    SELECT COUNT(*) FROM INFORMATION_SCHEMA.STATISTICS
    WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'Member' AND INDEX_NAME = 'idx_member_ancestor_path'
);
SET @sql = IF(@index_exists = 0,
    'CREATE INDEX idx_member_ancestor_path ON Member(ancestor_path)',
    'SELECT ''idx_member_ancestor_path already exists'''
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- ============================================================
-- 2. 全文搜索索引（替代 LIKE '%keyword%'）
--    中文必须用 ngram 分词器，默认空格分词对中文无效
-- ============================================================

-- 先删除旧索引（可能无 ngram 分词器）
SET @index_exists = (
    SELECT COUNT(*) FROM INFORMATION_SCHEMA.STATISTICS
    WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'Member' AND INDEX_NAME = 'ft_member_name'
);
SET @sql = IF(@index_exists > 0,
    'ALTER TABLE Member DROP INDEX ft_member_name',
    'SELECT ''ft_member_name does not exist, skip drop'''
);
PREPARE stmt FROM @sql;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- 重建索引，使用 ngram 分词器（支持中文）
ALTER TABLE Member ADD FULLTEXT INDEX ft_member_name (name) WITH PARSER ngram;

-- ============================================================
-- 3. 统计缓存表（替代 Redis）
-- ============================================================

CREATE TABLE IF NOT EXISTS StatisticsCache (
    cache_key VARCHAR(255) PRIMARY KEY COMMENT '缓存键（如 genealogy_1_stats）',
    cache_value JSON NOT NULL COMMENT '缓存的 JSON 数据',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    expires_at TIMESTAMP NOT NULL COMMENT '过期时间',
    INDEX idx_expires (expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='统计结果缓存表（替代 Redis）';

-- ============================================================
-- 4. 填充 ancestor_path（对现有数据）
-- 使用递归 CTE 从父亲线推导 path
-- ============================================================

UPDATE Member m
SET m.ancestor_path = (
    SELECT path FROM (
        WITH RECURSIVE path_cte AS (
            SELECT member_id, father_id, CONCAT('/', member_id) AS path
            FROM Member WHERE father_id IS NULL OR father_id = 0
            UNION ALL
            SELECT c.member_id, c.father_id, CONCAT(p.path, '/', c.member_id)
            FROM Member c INNER JOIN path_cte p ON c.father_id = p.member_id
        )
        SELECT path FROM path_cte WHERE member_id = m.member_id
    ) sub
)
WHERE m.ancestor_path IS NULL;
