-- 清理旧表
DROP TABLE IF EXISTS Marriage;
DROP TABLE IF EXISTS GenealogyShare;
DROP TABLE IF EXISTS Member;
DROP TABLE IF EXISTS Genealogy;
DROP TABLE IF EXISTS User;

-- 创建用户表
CREATE TABLE User (
    user_id INT AUTO_INCREMENT PRIMARY KEY COMMENT '用户ID',
    username VARCHAR(50) NOT NULL UNIQUE COMMENT '用户名',
    password VARCHAR(255) NOT NULL COMMENT '密码',
    status ENUM('active', 'inactive', 'suspended') DEFAULT 'active' COMMENT '用户状态',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 创建族谱表
CREATE TABLE Genealogy (
    genealogy_id INT AUTO_INCREMENT PRIMARY KEY COMMENT '族谱ID',
    name VARCHAR(100) NOT NULL COMMENT '族谱名称',
    surname VARCHAR(50) NOT NULL COMMENT '姓氏',
    founder_name VARCHAR(50) COMMENT '创始人姓名',
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    description TEXT COMMENT '族谱简介',
    user_id INT NOT NULL COMMENT '所属用户ID',
    FOREIGN KEY (user_id) REFERENCES User(user_id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 创建族谱共享表
CREATE TABLE GenealogyShare (
    share_id INT AUTO_INCREMENT PRIMARY KEY,
    genealogy_id INT NOT NULL,
    user_id INT NOT NULL,
    permission ENUM('view', 'edit') DEFAULT 'edit',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (genealogy_id) REFERENCES Genealogy(genealogy_id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES User(user_id) ON DELETE CASCADE,
    UNIQUE KEY unique_genealogy_user (genealogy_id, user_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 创建成员表（增加 generation 辈分列）
CREATE TABLE Member (
    member_id INT AUTO_INCREMENT PRIMARY KEY COMMENT '成员ID',
    genealogy_id INT NOT NULL COMMENT '所属族谱ID',
    name VARCHAR(50) NOT NULL COMMENT '姓名',
    gender ENUM('male', 'female') NOT NULL COMMENT '性别(male/female)',
    birth_date DATE COMMENT '出生日期',
    death_date DATE COMMENT '逝世日期',
    biography TEXT COMMENT '生平简介',
    father_id INT DEFAULT NULL COMMENT '父亲ID（必须为男性成员）',
    mother_id INT DEFAULT NULL COMMENT '母亲ID（必须为女性成员）',
    spouse_id INT DEFAULT NULL COMMENT '配偶ID（当前配偶，必须为异性）',
    generation INT NOT NULL DEFAULT 1 COMMENT '辈分（1为始祖，逐代递增）',
    ancestor_path VARCHAR(500) DEFAULT NULL COMMENT 'Materialized path: /1/15/234/567',
    FOREIGN KEY (genealogy_id) REFERENCES Genealogy(genealogy_id) ON DELETE CASCADE,
    FOREIGN KEY (father_id) REFERENCES Member(member_id) ON DELETE SET NULL,
    FOREIGN KEY (mother_id) REFERENCES Member(member_id) ON DELETE SET NULL,
    FOREIGN KEY (spouse_id) REFERENCES Member(member_id) ON DELETE SET NULL,
    -- 性别必须为合法值
    CONSTRAINT chk_gender CHECK (gender IN ('male', 'female')),
    -- 辈分必须为正整数（至少为 1）
    CONSTRAINT chk_generation_positive CHECK (generation >= 1),
    -- 出生日期必须在逝世日期之前（或相等）
    CONSTRAINT chk_birth_before_death CHECK (birth_date IS NULL OR death_date IS NULL OR birth_date <= death_date),
    -- ancestor_path 格式约束：必须以 / 开头
    CONSTRAINT chk_ancestor_path_format CHECK (ancestor_path IS NULL OR (ancestor_path LIKE '/%'))
    -- 注意：self-reference 和 father!=mother 约束在 C++ 应用层（validateParentIds）中强制执行
    -- MySQL 8.0 不允许对 ON DELETE SET NULL 的外键列添加 CHECK 约束
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 创建婚姻表（记录婚姻历史）
CREATE TABLE Marriage (
    marriage_id INT AUTO_INCREMENT PRIMARY KEY COMMENT '婚姻ID',
    husband_id INT NOT NULL COMMENT '丈夫ID（必须为男性）',
    wife_id INT NOT NULL COMMENT '妻子ID（必须为女性）',
    wedding_date DATE COMMENT '结婚日期',
    status ENUM('active', 'dissolved') DEFAULT 'active' COMMENT '婚姻状态',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    FOREIGN KEY (husband_id) REFERENCES Member(member_id) ON DELETE CASCADE,
    FOREIGN KEY (wife_id) REFERENCES Member(member_id) ON DELETE CASCADE,
    -- 丈夫和妻子不能是同一个人
    CONSTRAINT chk_husband_not_wife CHECK (husband_id != wife_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- ============================================================
-- 索引优化（支持10万条数据高效查询）
-- ============================================================

-- 辈分统计索引（核心：替代递归CTE，直接GROUP BY generation）
CREATE INDEX idx_member_genealogy_gen ON Member(genealogy_id, generation);

-- 血缘关系索引（加速递归CTE：查找父亲/母亲）
CREATE INDEX idx_member_father ON Member(father_id);
CREATE INDEX idx_member_mother ON Member(mother_id);
CREATE INDEX idx_member_father_mother ON Member(genealogy_id, father_id, mother_id);

-- 统计分析复合索引
-- 寿命统计：按族谱过滤 + 确保有生卒日期
CREATE INDEX idx_member_lifespan ON Member(genealogy_id, generation, birth_date, death_date);

-- 未婚男性>50岁查询
CREATE INDEX idx_member_unmarried_men ON Member(genealogy_id, gender, spouse_id, birth_date);

-- 出生年份统计
CREATE INDEX idx_member_birth_gen ON Member(genealogy_id, generation, birth_date);

-- 单列索引
CREATE INDEX idx_member_birth_date ON Member(birth_date);
CREATE INDEX idx_member_death_date ON Member(death_date);
CREATE INDEX idx_member_spouse ON Member(spouse_id);
CREATE INDEX idx_member_name ON Member(name);
CREATE INDEX idx_member_ancestor_path ON Member(ancestor_path);

-- 全文搜索索引（Phase 3：替代 LIKE '%keyword%'）
-- 中文需 ngram 分词器，否则默认空格分词对中文无效
ALTER TABLE Member ADD FULLTEXT INDEX ft_member_name (name) WITH PARSER ngram;

-- ============================================================
-- 统计缓存表（Phase 3：替代 Redis 的内存缓存方案）
-- ============================================================
CREATE TABLE IF NOT EXISTS StatisticsCache (
    cache_key VARCHAR(255) PRIMARY KEY COMMENT '缓存键（如 genealogy_1_stats）',
    cache_value JSON NOT NULL COMMENT '缓存的 JSON 数据',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    expires_at TIMESTAMP NOT NULL COMMENT '过期时间',
    INDEX idx_expires (expires_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='统计结果缓存表（替代 Redis）';
