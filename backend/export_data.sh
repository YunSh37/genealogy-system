#!/bin/bash
# ============================================================
# 家谱系统数据导出脚本
# 与 import_wsl.sh 配套，导出数据可重新导入
# ============================================================
# 用法:
#   ./export_data.sh --full <genealogy_id>              全量导出
#   ./export_data.sh --branch <genealogy_id> <root_id>  分支导出
#   ./export_data.sh --all                               导出所有族谱
#   ./export_data.sh --list                              列出所有族谱
#   ./export_data.sh --full <id> --with-generation       含generation列
# ============================================================

set -e

MYSQL="${MYSQL_CMD:-mysql -u root -p123456 genealogy_db}"
EXPORT_BASE="./exports"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
WITH_GENERATION=false
MAX_DEPTH=${MAX_DEPTH:-30}

# ============================================================
# 工具函数
# ============================================================

usage() {
    echo "用法:"
    echo "  $0 --full <genealogy_id>              全量导出整个族谱"
    echo "  $0 --full <genealogy_id> --with-generation  全量导出(含generation列)"
    echo "  $0 --branch <genealogy_id> <root_id>  导出指定分支"
    echo "  $0 --all                               导出所有族谱"
    echo "  $0 --list                              列出所有族谱"
    echo ""
    echo "导出目录: $EXPORT_BASE/"
    exit 1
}

log()  { echo "[$(date '+%H:%M:%S')] $*"; }
warn() { echo "[$(date '+%H:%M:%S')] ⚠ $*"; }

# ============================================================
# 列出所有族谱
# ============================================================
list_genealogies() {
    log "=== 数据库中所有族谱 ==="
    $MYSQL -t -e "
        SELECT g.genealogy_id, g.name, g.surname,
               COUNT(DISTINCT m.member_id) AS member_count,
               g.create_time
        FROM Genealogy g
        LEFT JOIN Member m ON g.genealogy_id = m.genealogy_id
        GROUP BY g.genealogy_id
        ORDER BY g.genealogy_id;
    "
}

# ============================================================
# 全量导出：导出整个族谱的所有成员和婚姻
# ============================================================
export_full() {
    local gid=$1
    local dir="$EXPORT_BASE/genealogy_${gid}_full_${TIMESTAMP}"
    mkdir -p "$dir"

    log "=== 全量导出 族谱ID=$gid ==="
    log "输出目录: $dir"

    # 1. 导出族谱信息
    log "导出族谱信息..."
    $MYSQL -N -e "
        SELECT genealogy_id, name, surname, founder_name, create_time, description, user_id
        FROM Genealogy WHERE genealogy_id = $gid
    " 2>/dev/null | while IFS=$'\t' read -r line; do
        echo "\"$(echo "$line" | sed 's/\t/\",\"/g')\""
    done > "$dir/genealogy.csv"

    # 重写 genealogy.csv 为正确格式
    $MYSQL -N -e "
        SELECT CONCAT_WS(',',
            QUOTE(genealogy_id), QUOTE(name), QUOTE(surname),
            QUOTE(IFNULL(founder_name,'')), QUOTE(create_time),
            QUOTE(IFNULL(description,'')), QUOTE(user_id)
        )
        FROM Genealogy WHERE genealogy_id = $gid
    " 2>/dev/null > "$dir/genealogy.csv"

    # 添加 CSV 头
    sed -i '1i genealogy_id,name,surname,founder_name,create_time,description,user_id' "$dir/genealogy.csv"

    local genealogy_count=$($MYSQL -N -e "SELECT COUNT(*) FROM Genealogy WHERE genealogy_id = $gid")
    log "  族谱记录: $genealogy_count"

    # 2. 导出成员信息
    log "导出成员信息..."

    if $WITH_GENERATION; then
        $MYSQL -N -e "
            SELECT CONCAT_WS(',',
                QUOTE(member_id), QUOTE(genealogy_id), QUOTE(name),
                QUOTE(gender), QUOTE(IFNULL(birth_date,'')),
                QUOTE(IFNULL(death_date,'')), QUOTE(IFNULL(biography,'')),
                QUOTE(IFNULL(father_id,'')), QUOTE(IFNULL(mother_id,'')),
                QUOTE(IFNULL(spouse_id,'')), QUOTE(IFNULL(generation,''))
            )
            FROM Member WHERE genealogy_id = $gid
            ORDER BY member_id
        " 2>/dev/null > "$dir/member.csv"
        sed -i '1i member_id,genealogy_id,name,gender,birth_date,death_date,biography,father_id,mother_id,spouse_id,generation' "$dir/member.csv"
    else
        $MYSQL -N -e "
            SELECT CONCAT_WS(',',
                QUOTE(member_id), QUOTE(genealogy_id), QUOTE(name),
                QUOTE(gender), QUOTE(IFNULL(birth_date,'')),
                QUOTE(IFNULL(death_date,'')), QUOTE(IFNULL(biography,'')),
                QUOTE(IFNULL(father_id,'')), QUOTE(IFNULL(mother_id,'')),
                QUOTE(IFNULL(spouse_id,''))
            )
            FROM Member WHERE genealogy_id = $gid
            ORDER BY member_id
        " 2>/dev/null > "$dir/member.csv"
        sed -i '1i member_id,genealogy_id,name,gender,birth_date,death_date,biography,father_id,mother_id,spouse_id' "$dir/member.csv"
    fi

    local member_count=$($MYSQL -N -e "SELECT COUNT(*) FROM Member WHERE genealogy_id = $gid")
    log "  成员记录: $member_count"

    # 3. 导出婚姻信息
    log "导出婚姻信息..."
    local member_ids=$($MYSQL -N -e "SELECT GROUP_CONCAT(member_id) FROM Member WHERE genealogy_id = $gid")

    if [ -n "$member_ids" ]; then
        $MYSQL -N -e "
            SELECT CONCAT_WS(',',
                QUOTE(marriage_id), QUOTE(husband_id), QUOTE(wife_id),
                QUOTE(IFNULL(wedding_date,'')), QUOTE(status),
                QUOTE(created_at)
            )
            FROM Marriage
            WHERE husband_id IN ($member_ids) OR wife_id IN ($member_ids)
            ORDER BY marriage_id
        " 2>/dev/null > "$dir/marriage.csv"
        sed -i '1i marriage_id,husband_id,wife_id,wedding_date,status,created_at' "$dir/marriage.csv"
    else
        echo "marriage_id,husband_id,wife_id,wedding_date,status,created_at" > "$dir/marriage.csv"
    fi

    local marriage_count=$($MYSQL -N -e "
        SELECT COUNT(*) FROM Marriage
        WHERE husband_id IN (SELECT member_id FROM Member WHERE genealogy_id = $gid)
           OR wife_id IN (SELECT member_id FROM Member WHERE genealogy_id = $gid)
    ")
    log "  婚姻记录: $marriage_count"

    # 4. 导出元信息
    cat > "$dir/export_info.txt" << EOF
# 家谱系统数据导出
导出时间: $(date '+%Y-%m-%d %H:%M:%S')
导出模式: 全量导出
族谱ID: $gid
族谱名称: $($MYSQL -N -e "SELECT name FROM Genealogy WHERE genealogy_id = $gid")
成员数量: $member_count
婚姻记录: $marriage_count
含generation列: $WITH_GENERATION
EOF

    log "=== 导出完成 ==="
    log "输出目录: $dir"
    log "文件列表:"
    ls -lh "$dir/"
    echo ""
    log "重新导入命令:"
    echo "  cp $dir/*.csv /mnt/d/shujuku/test_data/"
    echo "  # 然后运行 import_wsl.sh"
}

# ============================================================
# 分支导出：导出指定成员及其祖先+后代
# ============================================================
export_branch() {
    local gid=$1
    local root_id=$2
    local dir="$EXPORT_BASE/genealogy_${gid}_branch_${root_id}_${TIMESTAMP}"
    mkdir -p "$dir"

    log "=== 分支导出 族谱ID=$gid 根成员ID=$root_id ==="
    log "输出目录: $dir"

    # 验证根成员存在
    local root_check=$($MYSQL -N -e "
        SELECT COUNT(*) FROM Member
        WHERE member_id = $root_id AND genealogy_id = $gid
    ")
    if [ "$root_check" = "0" ]; then
        warn "成员ID=$root_id 在族谱ID=$gid 中不存在"
        exit 1
    fi

    local root_name=$($MYSQL -N -e "SELECT name FROM Member WHERE member_id = $root_id")
    log "根成员: $root_name (ID=$root_id)"

    # 使用普通表（带唯一后缀）替代临时表，避免会话隔离问题
    local suffix="_${$}_${TIMESTAMP}"
    local anc_table="branch_ancestors${suffix}"
    local desc_table="branch_descendants${suffix}"

    # 清理可能残留的同名表
    $MYSQL -N -e "DROP TABLE IF EXISTS $anc_table; DROP TABLE IF EXISTS $desc_table;" 2>/dev/null

    # === 关键修复：拆分为两个独立的单向递归 CTE，避免循环引用 ===

    # 1. 查找所有祖先（向上 CTE，拆分 father/mother 以利用索引）
    log "查找祖先（向上递归）..."
    $MYSQL -N -e "
        CREATE TABLE $anc_table (
            member_id INT PRIMARY KEY
        );

        INSERT INTO $anc_table (member_id)
        WITH RECURSIVE ancestors AS (
            SELECT member_id, father_id, mother_id, 0 as depth
            FROM Member
            WHERE member_id = $root_id AND genealogy_id = $gid

            UNION ALL

            SELECT m.member_id, m.father_id, m.mother_id, a.depth + 1
            FROM Member m
            JOIN ancestors a ON m.member_id = a.father_id
            WHERE m.genealogy_id = $gid
              AND a.father_id IS NOT NULL AND a.father_id > 0
              AND a.depth < $MAX_DEPTH

            UNION ALL

            SELECT m.member_id, m.father_id, m.mother_id, a.depth + 1
            FROM Member m
            JOIN ancestors a ON m.member_id = a.mother_id
            WHERE m.genealogy_id = $gid
              AND a.mother_id IS NOT NULL AND a.mother_id > 0
              AND a.depth < $MAX_DEPTH
        )
        SELECT DISTINCT member_id FROM ancestors WHERE depth > 0;
    " 2>/dev/null

    local ancestor_count=$($MYSQL -N -e "SELECT COUNT(*) FROM $anc_table" 2>/dev/null || echo 0)
    log "  祖先: $ancestor_count 人"

    # 2. 查找所有后代（向下 CTE，拆分 father/mother 以利用索引）
    log "查找后代（向下递归）..."
    $MYSQL -N -e "
        CREATE TABLE $desc_table (
            member_id INT PRIMARY KEY
        );

        INSERT INTO $desc_table (member_id)
        WITH RECURSIVE descendants AS (
            SELECT member_id, 0 as depth
            FROM Member
            WHERE member_id = $root_id AND genealogy_id = $gid

            UNION ALL

            SELECT m.member_id, d.depth + 1
            FROM Member m
            JOIN descendants d ON m.father_id = d.member_id
            WHERE m.genealogy_id = $gid
              AND d.depth < $MAX_DEPTH

            UNION ALL

            SELECT m.member_id, d.depth + 1
            FROM Member m
            JOIN descendants d ON m.mother_id = d.member_id
            WHERE m.genealogy_id = $gid
              AND d.depth < $MAX_DEPTH
        )
        SELECT DISTINCT member_id FROM descendants WHERE depth > 0;
    " 2>/dev/null

    local descendant_count=$($MYSQL -N -e "SELECT COUNT(*) FROM $desc_table" 2>/dev/null || echo 0)
    log "  后代: $descendant_count 人"

    # 3. 合并所有分支成员 ID（使用普通表名）
    local all_ids_sql="
        SELECT member_id FROM Member WHERE member_id = $root_id
        UNION
        SELECT member_id FROM $anc_table
        UNION
        SELECT member_id FROM $desc_table
    "
    local total_branch=$($MYSQL -N -e "SELECT COUNT(*) FROM ($all_ids_sql) t" 2>/dev/null)
    log "  分支总计: $total_branch 人"

    # 4. 导出族谱信息
    log "导出族谱信息..."
    $MYSQL -N -e "
        SELECT CONCAT_WS(',',
            QUOTE(genealogy_id), QUOTE(name), QUOTE(surname),
            QUOTE(IFNULL(founder_name,'')), QUOTE(create_time),
            QUOTE(IFNULL(description,'')), QUOTE(user_id)
        )
        FROM Genealogy WHERE genealogy_id = $gid
    " > "$dir/genealogy.csv"
    sed -i '1i genealogy_id,name,surname,founder_name,create_time,description,user_id' "$dir/genealogy.csv"

    # 5. 导出分支成员
    log "导出成员信息..."
    if $WITH_GENERATION; then
        $MYSQL -N -e "
            SELECT CONCAT_WS(',',
                QUOTE(m.member_id), QUOTE(m.genealogy_id), QUOTE(m.name),
                QUOTE(m.gender), QUOTE(IFNULL(m.birth_date,'')),
                QUOTE(IFNULL(m.death_date,'')), QUOTE(IFNULL(m.biography,'')),
                QUOTE(IFNULL(m.father_id,'')), QUOTE(IFNULL(m.mother_id,'')),
                QUOTE(IFNULL(m.spouse_id,'')), QUOTE(IFNULL(m.generation,''))
            )
            FROM Member m
            JOIN ($all_ids_sql) ids ON m.member_id = ids.member_id
            ORDER BY m.member_id
        " > "$dir/member.csv"
        sed -i '1i member_id,genealogy_id,name,gender,birth_date,death_date,biography,father_id,mother_id,spouse_id,generation' "$dir/member.csv"
    else
        $MYSQL -N -e "
            SELECT CONCAT_WS(',',
                QUOTE(m.member_id), QUOTE(m.genealogy_id), QUOTE(m.name),
                QUOTE(m.gender), QUOTE(IFNULL(m.birth_date,'')),
                QUOTE(IFNULL(m.death_date,'')), QUOTE(IFNULL(m.biography,'')),
                QUOTE(IFNULL(m.father_id,'')), QUOTE(IFNULL(m.mother_id,'')),
                QUOTE(IFNULL(m.spouse_id,''))
            )
            FROM Member m
            JOIN ($all_ids_sql) ids ON m.member_id = ids.member_id
            ORDER BY m.member_id
        " > "$dir/member.csv"
        sed -i '1i member_id,genealogy_id,name,gender,birth_date,death_date,biography,father_id,mother_id,spouse_id' "$dir/member.csv"
    fi

    # 6. 导出分支成员的婚姻
    log "导出婚姻信息..."
    $MYSQL -N -e "
        SELECT CONCAT_WS(',',
            QUOTE(mr.marriage_id), QUOTE(mr.husband_id), QUOTE(mr.wife_id),
            QUOTE(IFNULL(mr.wedding_date,'')), QUOTE(mr.status),
            QUOTE(mr.created_at)
        )
        FROM Marriage mr
        WHERE mr.husband_id IN ($all_ids_sql)
           OR mr.wife_id IN ($all_ids_sql)
        ORDER BY mr.marriage_id
    " > "$dir/marriage.csv"
    sed -i '1i marriage_id,husband_id,wife_id,wedding_date,status,created_at' "$dir/marriage.csv"

    local marriage_count=$(wc -l < "$dir/marriage.csv")
    marriage_count=$((marriage_count - 1))  # 减去表头

    # 7. 导出元信息
    cat > "$dir/export_info.txt" << EOF
# 家谱系统数据导出
导出时间: $(date '+%Y-%m-%d %H:%M:%S')
导出模式: 分支导出
族谱ID: $gid
族谱名称: $($MYSQL -N -e "SELECT name FROM Genealogy WHERE genealogy_id = $gid")
根成员ID: $root_id
根成员姓名: $root_name
祖先数量: $ancestor_count
后代数量: $descendant_count
分支总人数: $total_branch
婚姻记录: $marriage_count
含generation列: $WITH_GENERATION
EOF

    # 清理临时表
    $MYSQL -N -e "DROP TABLE IF EXISTS $anc_table; DROP TABLE IF EXISTS $desc_table;" 2>/dev/null || true

    log "=== 导出完成 ==="
    log "输出目录: $dir"
    log "文件列表:"
    ls -lh "$dir/"
    echo ""
    log "分支结构: 祖先 $ancestor_count 代 ← [$root_name] → 后代 $descendant_count 人"
}

# ============================================================
# 导出所有族谱
# ============================================================
export_all() {
    log "=== 导出所有族谱 ==="

    local gids=$($MYSQL -N -e "SELECT genealogy_id FROM Genealogy ORDER BY genealogy_id")

    if [ -z "$gids" ]; then
        warn "数据库中没有族谱记录"
        exit 1
    fi

    for gid in $gids; do
        local name=$($MYSQL -N -e "SELECT name FROM Genealogy WHERE genealogy_id = $gid")
        log ""
        log ">>> 导出: $name (ID=$gid)"
        export_full "$gid"
    done

    log ""
    log "=== 全部导出完成 ==="
    ls -d "$EXPORT_BASE"/*/
}

# ============================================================
# 主入口
# ============================================================

# 解析参数
MODE=""
GENEALOGY_ID=""
ROOT_ID=""

while [ $# -gt 0 ]; do
    case "$1" in
        --full)
            MODE="full"
            GENEALOGY_ID="$2"
            shift 2
            ;;
        --branch)
            MODE="branch"
            GENEALOGY_ID="$2"
            ROOT_ID="$3"
            shift 3
            ;;
        --all)
            MODE="all"
            shift
            ;;
        --list)
            MODE="list"
            shift
            ;;
        --with-generation)
            WITH_GENERATION=true
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "未知参数: $1"
            usage
            ;;
    esac
done

# 检查 MySQL 连接
if ! $MYSQL -e "SELECT 1" >/dev/null 2>&1; then
    warn "无法连接 MySQL，请确认数据库正在运行"
    exit 1
fi

# 执行对应模式
case "$MODE" in
    full)
        if [ -z "$GENEALOGY_ID" ]; then
            warn "请指定族谱ID: $0 --full <genealogy_id>"
            exit 1
        fi
        export_full "$GENEALOGY_ID"
        ;;
    branch)
        if [ -z "$GENEALOGY_ID" ] || [ -z "$ROOT_ID" ]; then
            warn "请指定族谱ID和根成员ID: $0 --branch <genealogy_id> <root_id>"
            exit 1
        fi
        export_branch "$GENEALOGY_ID" "$ROOT_ID"
        ;;
    all)
        export_all
        ;;
    list)
        list_genealogies
        ;;
    *)
        usage
        ;;
esac