-- ============================================================
-- 辈分修复迁移脚本（BFS 逐层推进版，利用索引高效计算）
-- ============================================================
-- 执行方式: mysql -u root -p123456 genealogy_db < migration_fix_generations.sql
-- 预计耗时: 10~30 秒（10 万成员，深度 30）
--
-- 核心优化（相比旧版迭代法）：
--   旧版：每轮子查询 JOIN 全表（OR 条件导致索引失效），45+ 轮 → 数十分钟
--   新版：每轮 WHERE p.generation = N（索引命中），拆分 father/mother 独立 JOIN
--         每轮只处理当前层的少量节点，30 轮 → 10~30 秒
-- ============================================================

SELECT '===== 开始迁移 =====' AS status, NOW() AS time;

-- 检查当前状态
SELECT '迁移前' AS 阶段, COUNT(*) AS 总成员,
       SUM(generation <= 0) AS generation异常,
       SUM(ancestor_path IS NULL) AS path为空
FROM Member;

-- ============================================================
-- 1. 修复 generation（BFS 逐层推进）
-- ============================================================

ALTER TABLE Member DROP CONSTRAINT chk_generation_positive;

-- 全部重置为 0（哨兵值，表示"未计算"）
UPDATE Member SET generation = 0;

-- 根节点 = 第 1 代（无父无母）
UPDATE Member SET generation = 1
WHERE father_id IS NULL AND mother_id IS NULL;

SELECT '根节点' AS 阶段, COUNT(*) AS 数量 FROM Member WHERE generation = 1;

-- BFS 循环：每次找到当前代的所有成员，将他们的子代设为 current_gen + 1
-- 拆分为 father/mother 两条 UPDATE，各自利用独立索引
DELIMITER //
CREATE PROCEDURE _fix_gen_bfs()
BEGIN
    DECLARE cur INT DEFAULT 1;

    _loop: WHILE cur <= 100 DO
        -- 检查当前代是否有成员（没有则说明已到叶子层，循环结束）
        IF (SELECT COUNT(*) FROM Member WHERE generation = cur) = 0 THEN
            LEAVE _loop;
        END IF;

        -- 通过父亲索引：找到 generation=cur 的成员的所有子代
        UPDATE Member child
        JOIN Member parent ON child.father_id = parent.member_id
        SET child.generation = cur + 1
        WHERE parent.generation = cur AND child.generation <= 0;

        -- 通过母亲索引：找到 generation=cur 的成员的所有子代
        UPDATE Member child
        JOIN Member parent ON child.mother_id = parent.member_id
        SET child.generation = cur + 1
        WHERE parent.generation = cur AND child.generation <= 0;

        SET cur = cur + 1;
    END WHILE _loop;

    -- 兜底：孤立节点设为第 1 代
    UPDATE Member SET generation = 1 WHERE generation <= 0;
END//
DELIMITER ;

CALL _fix_gen_bfs();
DROP PROCEDURE _fix_gen_bfs;

SELECT 'Generation 修复完成' AS status,
       COUNT(*) AS total,
       MIN(generation) AS min_gen,
       MAX(generation) AS max_gen,
       SUM(generation <= 0) AS still_bad
FROM Member;

-- ============================================================
-- 2. 修复 ancestor_path（BFS 逐层推进）
-- ============================================================

UPDATE Member SET ancestor_path = NULL;

-- 根节点
UPDATE Member SET ancestor_path = CONCAT('/', member_id)
WHERE father_id IS NULL AND mother_id IS NULL;

-- BFS：每层从父亲（优先）继承路径
DELIMITER //
CREATE PROCEDURE _fix_path_bfs()
BEGIN
    DECLARE cur INT DEFAULT 1;

    _loop: WHILE cur <= 100 DO
        IF (SELECT COUNT(*) FROM Member WHERE generation = cur AND ancestor_path IS NOT NULL) = 0 THEN
            LEAVE _loop;
        END IF;

        -- 优先从父亲继承路径
        UPDATE Member child
        JOIN Member parent ON child.father_id = parent.member_id
        SET child.ancestor_path = CONCAT(parent.ancestor_path, '/', child.member_id)
        WHERE parent.generation = cur
          AND parent.ancestor_path IS NOT NULL
          AND child.ancestor_path IS NULL;

        -- 母亲路径（补充父亲未知的）
        UPDATE Member child
        JOIN Member parent ON child.mother_id = parent.member_id
        SET child.ancestor_path = CONCAT(parent.ancestor_path, '/', child.member_id)
        WHERE parent.generation = cur
          AND parent.ancestor_path IS NOT NULL
          AND child.ancestor_path IS NULL;

        SET cur = cur + 1;
    END WHILE _loop;

    -- 兜底
    UPDATE Member SET ancestor_path = CONCAT('/', member_id)
    WHERE ancestor_path IS NULL;
END//
DELIMITER ;

CALL _fix_path_bfs();
DROP PROCEDURE _fix_path_bfs;

SELECT 'Ancestor path 修复完成' AS status,
       SUM(ancestor_path IS NULL) AS still_null
FROM Member;

-- ============================================================
-- 3. 恢复约束
-- ============================================================

ALTER TABLE Member ADD CONSTRAINT chk_generation_positive
    CHECK (generation IS NOT NULL AND generation >= 1);

ALTER TABLE Member DROP CONSTRAINT chk_birth_before_death;
ALTER TABLE Member ADD CONSTRAINT chk_birth_before_death
    CHECK (birth_date IS NULL OR death_date IS NULL OR birth_date <= death_date);

ALTER TABLE Member DROP CONSTRAINT chk_ancestor_path_format;
ALTER TABLE Member ADD CONSTRAINT chk_ancestor_path_format
    CHECK (ancestor_path IS NULL OR (ancestor_path LIKE '/%'));

ALTER TABLE Marriage DROP CONSTRAINT chk_husband_not_wife;
ALTER TABLE Marriage ADD CONSTRAINT chk_husband_not_wife
    CHECK (husband_id != wife_id);

-- ============================================================
-- 4. 验证
-- ============================================================

SELECT '===== 迁移全部完成 =====' AS status,
       COUNT(*) AS total_members,
       MIN(generation) AS min_gen,
       MAX(generation) AS max_gen,
       ROUND(AVG(generation), 2) AS avg_gen
FROM Member;

SELECT generation AS 辈分, COUNT(*) AS 成员数
FROM Member
GROUP BY generation
ORDER BY generation;
