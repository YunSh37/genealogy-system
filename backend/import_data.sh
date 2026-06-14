#!/bin/bash
# ============================================================
# 家谱系统数据导入脚本（WSL / Linux 通用）
# 将 CSV 文件批量导入 MySQL 数据库
# ============================================================
# 用法:
#   ./import_data.sh <数据目录路径>
#
# 环境变量（可选）:
#   MYSQL_HOST      MySQL 主机地址（默认: 127.0.0.1）
#   MYSQL_PORT      MySQL 端口（默认: 3306）
#   MYSQL_USER      MySQL 用户名（默认: root）
#   MYSQL_PASSWORD  MySQL 密码（默认: 无密码时自动使用 sudo mysql）
#   MYSQL_DATABASE  数据库名称（默认: genealogy_db）
# ============================================================

set -e

# ---- 配置 ----
MYSQL_HOST="${MYSQL_HOST:-127.0.0.1}"
MYSQL_PORT="${MYSQL_PORT:-3306}"
MYSQL_USER="${MYSQL_USER:-root}"
MYSQL_DATABASE="${MYSQL_DATABASE:-genealogy_db}"

# ---- 密码处理 ----
# 如果设置了 MYSQL_PASSWORD 环境变量则直接使用
# 如果未设置且用户输入为空，则尝试使用 sudo mysql（WSL auth_socket 认证）
if [ -z "$MYSQL_PASSWORD" ]; then
    read -rsp "请输入 MySQL 密码（用户=$MYSQL_USER，无密码直接回车将使用 sudo）: " MYSQL_PASSWORD
    echo ""
fi

if [ -z "$MYSQL_PASSWORD" ]; then
    # 无密码模式：使用 sudo mysql（适用于 WSL/Ubuntu 的 auth_socket 认证）
    echo "使用 sudo mysql（无密码）..."
    MYSQL_CMD="sudo mysql --local-infile=1 -h $MYSQL_HOST -P $MYSQL_PORT -u $MYSQL_USER"
    # 连接检查也用 sudo
    MYSQL_CHECK="sudo mysql -h $MYSQL_HOST -P $MYSQL_PORT -u $MYSQL_USER"
else
    MYSQL_CMD="mysql --local-infile=1 -h $MYSQL_HOST -P $MYSQL_PORT -u $MYSQL_USER -p$MYSQL_PASSWORD"
    MYSQL_CHECK="mysql -h $MYSQL_HOST -P $MYSQL_PORT -u $MYSQL_USER -p$MYSQL_PASSWORD"
fi

# ---- 数据目录 ----
DATA_DIR="${1:-}"
if [ -z "$DATA_DIR" ]; then
    echo "用法: $0 <数据目录路径>"
    echo ""
    echo "数据目录中应包含以下 CSV 文件:"
    echo "  user.csv, genealogy.csv, member.csv, marriage.csv, genealogy_share.csv"
    echo ""
    echo "环境变量:"
    echo "  MYSQL_HOST / MYSQL_PORT / MYSQL_USER / MYSQL_PASSWORD / MYSQL_DATABASE"
    echo ""
    echo "示例:"
    echo "  export MYSQL_PASSWORD=123456"
    echo "  ./import_data.sh migrations/test_data"
    echo ""
    echo "  # 或使用 sudo 免密模式（WSL 默认）:"
    echo "  ./import_data.sh migrations/test_data  # 密码提示时直接回车"
    exit 1
fi

if [ ! -d "$DATA_DIR" ]; then
    echo "错误: 数据目录不存在: $DATA_DIR"
    exit 1
fi
DATA_DIR="$(cd "$DATA_DIR" && pwd)"

echo "=========================================="
echo "  家谱系统数据导入"
echo "=========================================="
echo "MySQL: $MYSQL_HOST:$MYSQL_PORT/$MYSQL_DATABASE"
echo "数据:  $DATA_DIR"
echo "=========================================="

# ---- 文件检查 ----
for f in user.csv genealogy.csv member.csv marriage.csv genealogy_share.csv; do
    if [ ! -f "$DATA_DIR/$f" ]; then
        echo "错误: 缺少文件: $f"; exit 1
    fi
    echo "  [OK] $f"
done

# ---- MySQL 连接检查 ----
if ! $MYSQL_CHECK -e "SELECT 1" "$MYSQL_DATABASE" >/dev/null 2>&1; then
    echo "错误: 无法连接 MySQL"
    echo "  请检查:"
    echo "    - MySQL 服务是否已启动: sudo service mysql status"
    echo "    - 数据库是否已创建: $MYSQL_DATABASE"
    echo "    - 用户名/密码是否正确"
    exit 1
fi

# ---- 导入 ----
M="$MYSQL_CMD $MYSQL_DATABASE"
D="$DATA_DIR"

echo ">>> 导入 User ..."
$M -e "LOAD DATA LOCAL INFILE '$D/user.csv' INTO TABLE User CHARACTER SET utf8mb4 FIELDS TERMINATED BY ',' ENCLOSED BY '\"' LINES TERMINATED BY '\n' IGNORE 1 ROWS (user_id, username, password, status, created_at);"
echo "  用户: $($M -N -e 'SELECT COUNT(*) FROM User')"

echo ">>> 导入 Genealogy ..."
$M -e "LOAD DATA LOCAL INFILE '$D/genealogy.csv' INTO TABLE Genealogy CHARACTER SET utf8mb4 FIELDS TERMINATED BY ',' ENCLOSED BY '\"' LINES TERMINATED BY '\n' IGNORE 1 ROWS (genealogy_id, name, surname, founder_name, create_time, description, user_id);"
echo "  族谱: $($M -N -e 'SELECT COUNT(*) FROM Genealogy')"

echo ">>> 导入 Member (10 万+ 条，可能需要 1-2 分钟) ..."
$M -e "SET FOREIGN_KEY_CHECKS = 0; LOAD DATA LOCAL INFILE '$D/member.csv' INTO TABLE Member CHARACTER SET utf8mb4 FIELDS TERMINATED BY ',' ENCLOSED BY '\"' LINES TERMINATED BY '\n' IGNORE 1 ROWS (member_id, genealogy_id, name, gender, birth_date, death_date, biography, father_id, mother_id, spouse_id); SET FOREIGN_KEY_CHECKS = 1;"
echo "  成员: $($M -N -e 'SELECT COUNT(*) FROM Member')"

echo ">>> 导入 Marriage ..."
$M -e "LOAD DATA LOCAL INFILE '$D/marriage.csv' INTO TABLE Marriage CHARACTER SET utf8mb4 FIELDS TERMINATED BY ',' ENCLOSED BY '\"' LINES TERMINATED BY '\n' IGNORE 1 ROWS (marriage_id, husband_id, wife_id, wedding_date, status, created_at);"
echo "  婚姻: $($M -N -e 'SELECT COUNT(*) FROM Marriage')"

echo ">>> 导入 GenealogyShare ..."
$M -e "LOAD DATA LOCAL INFILE '$D/genealogy_share.csv' INTO TABLE GenealogyShare CHARACTER SET utf8mb4 FIELDS TERMINATED BY ',' ENCLOSED BY '\"' LINES TERMINATED BY '\n' IGNORE 1 ROWS (share_id, genealogy_id, user_id, permission, created_at);"
echo "  共享: $($M -N -e 'SELECT COUNT(*) FROM GenealogyShare')"

# ---- 后处理 ----
echo ""
echo "=== 后处理: 计算 generation（辈分） & ancestor_path（物化路径） ==="

# ★ 关键：LOAD DATA 不含 generation 列，所有成员默认为 1
#   generation 列有 NOT NULL + CHECK(generation>=1) 约束，不能设 NULL 或 0
#   直接用 CTE 覆盖所有成员的正确辈分，不加 WHERE 过滤
$M -e "WITH RECURSIVE gen_calc AS (SELECT member_id, 1 as gen FROM Member WHERE father_id IS NULL AND mother_id IS NULL UNION ALL SELECT m.member_id, gc.gen + 1 FROM Member m JOIN gen_calc gc ON (m.father_id = gc.member_id OR m.mother_id = gc.member_id)) UPDATE Member m INNER JOIN gen_calc gc ON m.member_id = gc.member_id SET m.generation = gc.gen;" 2>/dev/null || true
echo "  最大辈分: 第 $($M -N -e 'SELECT MAX(generation) FROM Member') 代"

$M -e "UPDATE Member SET ancestor_path = CONCAT('/', member_id) WHERE father_id IS NULL AND mother_id IS NULL;"
for i in $(seq 1 30); do
    $M -e "UPDATE Member m JOIN Member p ON m.father_id = p.member_id SET m.ancestor_path = CONCAT(p.ancestor_path, '/', m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;" 2>/dev/null
    $M -e "UPDATE Member m JOIN Member p ON m.mother_id = p.member_id SET m.ancestor_path = CONCAT(p.ancestor_path, '/', m.member_id) WHERE m.ancestor_path IS NULL AND p.ancestor_path IS NOT NULL;" 2>/dev/null
    remain=$($M -N -e "SELECT COUNT(*) FROM Member WHERE ancestor_path IS NULL" 2>/dev/null || echo 0)
    [ "$remain" -eq 0 ] && break
done
$M -e "UPDATE Member SET ancestor_path = CONCAT('/', member_id) WHERE ancestor_path IS NULL;"

echo ""
echo "=========================================="
echo "  导入完成！"
echo "=========================================="
$M -t -e "SELECT 'User' AS 表, COUNT(*) AS 数量 FROM User UNION ALL SELECT 'Genealogy', COUNT(*) FROM Genealogy UNION ALL SELECT 'Member', COUNT(*) FROM Member UNION ALL SELECT 'Marriage', COUNT(*) FROM Marriage UNION ALL SELECT 'GenealogyShare', COUNT(*) FROM GenealogyShare;"
