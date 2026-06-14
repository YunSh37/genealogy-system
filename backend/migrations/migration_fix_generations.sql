-- ============================================================
-- 辈分修复迁移脚本（高效迭代版，适用于大规模数据）
-- ============================================================
-- 执行方式: mysql -u root -p123456 genealogy_db < migration_fix_generations.sql
-- ============================================================

-- 先检查当前状态
SELECT '开始迁移' AS status, COUNT(*) AS total_members,
       SUM(CASE WHEN generation <= 0 THEN 1 ELSE 0 END) AS bad_generation,
       SUM(CASE WHEN ancestor_path IS NULL THEN 1 ELSE 0 END) AS null_path
FROM Member;

-- 临时删除 CHECK 约束（迁移完成后重新添加）
ALTER TABLE Member DROP CONSTRAINT chk_generation_positive;

-- ============================================================
-- 1. 修复 generation 存量数据（迭代法，替代慢速递归 CTE）
--    每轮 UPDATE 将当前已知 generation 的成员的子代 generation 设为 parent_gen + 1
--    重复直到没有新成员被更新
--    注意：generation 列为 NOT NULL，使用 0 作为"未计算"哨兵值
-- ============================================================

-- 1a. 重置所有成员的 generation 为 0（未计算）
UPDATE Member SET generation = 0;

-- 1b. 设置根节点（无父无母）为第 1 代
UPDATE Member
SET generation = 1
WHERE (father_id IS NULL)
  AND (mother_id IS NULL);

-- 1c. 设置只有父亲为根的成员
UPDATE Member m
JOIN Member p ON m.father_id = p.member_id
SET m.generation = p.generation + 1
WHERE m.generation <= 0
  AND m.mother_id IS NULL
  AND p.generation > 0;

-- 1d. 设置只有母亲为根的成员
UPDATE Member m
JOIN Member p ON m.mother_id = p.member_id
SET m.generation = p.generation + 1
WHERE m.generation <= 0
  AND m.father_id IS NULL
  AND p.generation > 0;

-- 1e. 迭代：逐层推进，每次取 MAX(父辈 generation) + 1
--     重复执行直到 ROW_COUNT() = 0
-- 循环：每次处理一层
-- Round 2
UPDATE Member m
JOIN (
    SELECT m2.member_id, MAX(p.generation) + 1 AS new_gen
    FROM Member m2
    JOIN Member p ON m2.father_id = p.member_id OR m2.mother_id = p.member_id
    WHERE m2.generation <= 0 AND p.generation > 0
    GROUP BY m2.member_id
) calc ON m.member_id = calc.member_id
SET m.generation = calc.new_gen;

-- Round 3
UPDATE Member m
JOIN (
    SELECT m2.member_id, MAX(p.generation) + 1 AS new_gen
    FROM Member m2
    JOIN Member p ON m2.father_id = p.member_id OR m2.mother_id = p.member_id
    WHERE m2.generation <= 0 AND p.generation > 0
    GROUP BY m2.member_id
) calc ON m.member_id = calc.member_id
SET m.generation = calc.new_gen;

-- Round 4
UPDATE Member m
JOIN (
    SELECT m2.member_id, MAX(p.generation) + 1 AS new_gen
    FROM Member m2
    JOIN Member p ON m2.father_id = p.member_id OR m2.mother_id = p.member_id
    WHERE m2.generation <= 0 AND p.generation > 0
    GROUP BY m2.member_id
) calc ON m.member_id = calc.member_id
SET m.generation = calc.new_gen;

-- Round 5
UPDATE Member m
JOIN (
    SELECT m2.member_id, MAX(p.generation) + 1 AS new_gen
    FROM Member m2
    JOIN Member p ON m2.father_id = p.member_id OR m2.mother_id = p.member_id
    WHERE m2.generation <= 0 AND p.generation > 0
    GROUP BY m2.member_id
) calc ON m.member_id = calc.member_id
SET m.generation = calc.new_gen;

-- Round 6
UPDATE Member m
JOIN (
    SELECT m2.member_id, MAX(p.generation) + 1 AS new_gen
    FROM Member m2
    JOIN Member p ON m2.father_id = p.member_id OR m2.mother_id = p.member_id
    WHERE m2.generation <= 0 AND p.generation > 0
    GROUP BY m2.member_id
) calc ON m.member_id = calc.member_id
SET m.generation = calc.new_gen;

-- Round 7
UPDATE Member m
JOIN (
    SELECT m2.member_id, MAX(p.generation) + 1 AS new_gen
    FROM Member m2
    JOIN Member p ON m2.father_id = p.member_id OR m2.mother_id = p.member_id
    WHERE m2.generation <= 0 AND p.generation > 0
    GROUP BY m2.member_id
) calc ON m.member_id = calc.member_id
SET m.generation = calc.new_gen;

-- Round 8
UPDATE Member m
JOIN (
    SELECT m2.member_id, MAX(p.generation) + 1 AS new_gen
    FROM Member m2
    JOIN Member p ON m2.father_id = p.member_id OR m2.mother_id = p.member_id
    WHERE m2.generation <= 0 AND p.generation > 0
    GROUP BY m2.member_id
) calc ON m.member_id = calc.member_id
SET m.generation = calc.new_gen;

-- Round 9
UPDATE Member m
JOIN (
    SELECT m2.member_id, MAX(p.generation) + 1 AS new_gen
    FROM Member m2
    JOIN Member p ON m2.father_id = p.member_id OR m2.mother_id = p.member_id
    WHERE m2.generation <= 0 AND p.generation > 0
    GROUP BY m2.member_id
) calc ON m.member_id = calc.member_id
SET m.generation = calc.new_gen;

-- Round 10
UPDATE Member m
JOIN (
    SELECT m2.member_id, MAX(p.generation) + 1 AS new_gen
    FROM Member m2
    JOIN Member p ON m2.father_id = p.member_id OR m2.mother_id = p.member_id
    WHERE m2.generation <= 0 AND p.generation > 0
    GROUP BY m2.member_id
) calc ON m.member_id = calc.member_id
SET m.generation = calc.new_gen;

-- Round 11-15（覆盖更深的族谱）
UPDATE Member m JOIN (SELECT m2.member_id, MAX(p.generation)+1 AS ng FROM Member m2 JOIN Member p ON m2.father_id=p.member_id OR m2.mother_id=p.member_id WHERE m2.generation <= 0 AND p.generation > 0 GROUP BY m2.member_id) c ON m.member_id=c.member_id SET m.generation=c.ng;
UPDATE Member m JOIN (SELECT m2.member_id, MAX(p.generation)+1 AS ng FROM Member m2 JOIN Member p ON m2.father_id=p.member_id OR m2.mother_id=p.member_id WHERE m2.generation <= 0 AND p.generation > 0 GROUP BY m2.member_id) c ON m.member_id=c.member_id SET m.generation=c.ng;
UPDATE Member m JOIN (SELECT m2.member_id, MAX(p.generation)+1 AS ng FROM Member m2 JOIN Member p ON m2.father_id=p.member_id OR m2.mother_id=p.member_id WHERE m2.generation <= 0 AND p.generation > 0 GROUP BY m2.member_id) c ON m.member_id=c.member_id SET m.generation=c.ng;
UPDATE Member m JOIN (SELECT m2.member_id, MAX(p.generation)+1 AS ng FROM Member m2 JOIN Member p ON m2.father_id=p.member_id OR m2.mother_id=p.member_id WHERE m2.generation <= 0 AND p.generation > 0 GROUP BY m2.member_id) c ON m.member_id=c.member_id SET m.generation=c.ng;
UPDATE Member m JOIN (SELECT m2.member_id, MAX(p.generation)+1 AS ng FROM Member m2 JOIN Member p ON m2.father_id=p.member_id OR m2.mother_id=p.member_id WHERE m2.generation <= 0 AND p.generation > 0 GROUP BY m2.member_id) c ON m.member_id=c.member_id SET m.generation=c.ng;

-- Round 16-20
UPDATE Member m JOIN (SELECT m2.member_id, MAX(p.generation)+1 AS ng FROM Member m2 JOIN Member p ON m2.father_id=p.member_id OR m2.mother_id=p.member_id WHERE m2.generation <= 0 AND p.generation > 0 GROUP BY m2.member_id) c ON m.member_id=c.member_id SET m.generation=c.ng;
UPDATE Member m JOIN (SELECT m2.member_id, MAX(p.generation)+1 AS ng FROM Member m2 JOIN Member p ON m2.father_id=p.member_id OR m2.mother_id=p.member_id WHERE m2.generation <= 0 AND p.generation > 0 GROUP BY m2.member_id) c ON m.member_id=c.member_id SET m.generation=c.ng;
UPDATE Member m JOIN (SELECT m2.member_id, MAX(p.generation)+1 AS ng FROM Member m2 JOIN Member p ON m2.father_id=p.member_id OR m2.mother_id=p.member_id WHERE m2.generation <= 0 AND p.generation > 0 GROUP BY m2.member_id) c ON m.member_id=c.member_id SET m.generation=c.ng;
UPDATE Member m JOIN (SELECT m2.member_id, MAX(p.generation)+1 AS ng FROM Member m2 JOIN Member p ON m2.father_id=p.member_id OR m2.mother_id=p.member_id WHERE m2.generation <= 0 AND p.generation > 0 GROUP BY m2.member_id) c ON m.member_id=c.member_id SET m.generation=c.ng;
UPDATE Member m JOIN (SELECT m2.member_id, MAX(p.generation)+1 AS ng FROM Member m2 JOIN Member p ON m2.father_id=p.member_id OR m2.mother_id=p.member_id WHERE m2.generation <= 0 AND p.generation > 0 GROUP BY m2.member_id) c ON m.member_id=c.member_id SET m.generation=c.ng;

-- 处理剩余仍未设置 generation 的孤立节点
UPDATE Member SET generation = 1 WHERE generation <= 0;

SELECT 'Generation 修复完成' AS status,
       COUNT(*) AS total,
       SUM(CASE WHEN generation <= 0 THEN 1 ELSE 0 END) AS still_bad
FROM Member;

-- ============================================================
-- 2. 修复 ancestor_path 存量数据（迭代法）
-- ============================================================

-- 清空现有 ancestor_path
UPDATE Member SET ancestor_path = NULL;

-- 2a. 设置根节点
UPDATE Member
SET ancestor_path = CONCAT('/', member_id)
WHERE father_id IS NULL AND mother_id IS NULL;

-- 2b. 迭代重建路径（每轮处理一层）
-- Round 1: 根的子代
UPDATE Member m
JOIN Member p ON m.father_id = p.member_id
SET m.ancestor_path = CONCAT(p.ancestor_path, '/', m.member_id)
WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL AND m.mother_id IS NULL;

UPDATE Member m
JOIN Member p ON m.mother_id = p.member_id
SET m.ancestor_path = CONCAT(p.ancestor_path, '/', m.member_id)
WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL AND m.father_id IS NULL;

-- 有双亲的：优先从父亲路径继承
UPDATE Member m
JOIN Member p ON m.father_id = p.member_id
SET m.ancestor_path = CONCAT(p.ancestor_path, '/', m.member_id)
WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;

UPDATE Member m
JOIN Member p ON m.mother_id = p.member_id
SET m.ancestor_path = CONCAT(p.ancestor_path, '/', m.member_id)
WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;

-- Rounds 2-20: 继续逐层推进
UPDATE Member m JOIN Member p ON m.father_id=p.member_id SET m.ancestor_path=CONCAT(p.ancestor_path,'/',m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;
UPDATE Member m JOIN Member p ON m.mother_id=p.member_id SET m.ancestor_path=CONCAT(p.ancestor_path,'/',m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;
UPDATE Member m JOIN Member p ON m.father_id=p.member_id SET m.ancestor_path=CONCAT(p.ancestor_path,'/',m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;
UPDATE Member m JOIN Member p ON m.mother_id=p.member_id SET m.ancestor_path=CONCAT(p.ancestor_path,'/',m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;
UPDATE Member m JOIN Member p ON m.father_id=p.member_id SET m.ancestor_path=CONCAT(p.ancestor_path,'/',m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;
UPDATE Member m JOIN Member p ON m.mother_id=p.member_id SET m.ancestor_path=CONCAT(p.ancestor_path,'/',m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;
UPDATE Member m JOIN Member p ON m.father_id=p.member_id SET m.ancestor_path=CONCAT(p.ancestor_path,'/',m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;
UPDATE Member m JOIN Member p ON m.mother_id=p.member_id SET m.ancestor_path=CONCAT(p.ancestor_path,'/',m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;
UPDATE Member m JOIN Member p ON m.father_id=p.member_id SET m.ancestor_path=CONCAT(p.ancestor_path,'/',m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;
UPDATE Member m JOIN Member p ON m.mother_id=p.member_id SET m.ancestor_path=CONCAT(p.ancestor_path,'/',m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;
UPDATE Member m JOIN Member p ON m.father_id=p.member_id SET m.ancestor_path=CONCAT(p.ancestor_path,'/',m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;
UPDATE Member m JOIN Member p ON m.mother_id=p.member_id SET m.ancestor_path=CONCAT(p.ancestor_path,'/',m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;
UPDATE Member m JOIN Member p ON m.father_id=p.member_id SET m.ancestor_path=CONCAT(p.ancestor_path,'/',m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;
UPDATE Member m JOIN Member p ON m.mother_id=p.member_id SET m.ancestor_path=CONCAT(p.ancestor_path,'/',m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;
UPDATE Member m JOIN Member p ON m.father_id=p.member_id SET m.ancestor_path=CONCAT(p.ancestor_path,'/',m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;
UPDATE Member m JOIN Member p ON m.mother_id=p.member_id SET m.ancestor_path=CONCAT(p.ancestor_path,'/',m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;
UPDATE Member m JOIN Member p ON m.father_id=p.member_id SET m.ancestor_path=CONCAT(p.ancestor_path,'/',m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;
UPDATE Member m JOIN Member p ON m.mother_id=p.member_id SET m.ancestor_path=CONCAT(p.ancestor_path,'/',m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;
UPDATE Member m JOIN Member p ON m.father_id=p.member_id SET m.ancestor_path=CONCAT(p.ancestor_path,'/',m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;
UPDATE Member m JOIN Member p ON m.mother_id=p.member_id SET m.ancestor_path=CONCAT(p.ancestor_path,'/',m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;

-- 处理孤立节点
UPDATE Member SET ancestor_path = CONCAT('/', member_id) WHERE ancestor_path IS NULL;

SELECT 'Ancestor path 修复完成' AS status,
       SUM(CASE WHEN ancestor_path IS NULL THEN 1 ELSE 0 END) AS still_null
FROM Member;

-- ============================================================
-- 3. 添加 CHECK 约束
-- ============================================================

-- 3a. generation 必须为正整数（约束已在脚本开头删除，此处直接添加）
ALTER TABLE Member ADD CONSTRAINT chk_generation_positive
    CHECK (generation IS NOT NULL AND generation >= 1);

-- 3b. birth_date 必须在 death_date 之前
ALTER TABLE Member DROP CONSTRAINT chk_birth_before_death;
ALTER TABLE Member ADD CONSTRAINT chk_birth_before_death
    CHECK (birth_date IS NULL OR death_date IS NULL OR birth_date <= death_date);

-- 3c. ancestor_path 格式约束
ALTER TABLE Member DROP CONSTRAINT chk_ancestor_path_format;
ALTER TABLE Member ADD CONSTRAINT chk_ancestor_path_format
    CHECK (ancestor_path IS NULL OR (ancestor_path LIKE '/%'));

-- ============================================================
-- 4. Marriage 表约束
-- ============================================================

ALTER TABLE Marriage DROP CONSTRAINT chk_husband_not_wife;
ALTER TABLE Marriage ADD CONSTRAINT chk_husband_not_wife
    CHECK (husband_id != wife_id);

-- ============================================================
-- 5. 验证统计
-- ============================================================

SELECT '迁移全部完成' AS status,
       COUNT(*) AS total_members,
       MIN(generation) AS min_gen,
       MAX(generation) AS max_gen,
       ROUND(AVG(generation), 2) AS avg_gen
FROM Member;

SELECT generation, COUNT(*) AS count
FROM Member
GROUP BY generation
ORDER BY generation;