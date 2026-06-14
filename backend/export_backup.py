#!/usr/bin/env python3
"""
家谱系统数据导出脚本
支持：全量导出 / 分支导出（祖先+后代）
与 import_wsl.sh 配套，导出 CSV 可直接重新导入
"""
import csv
import sys
import os
import json
from datetime import datetime
import pymysql

# ============================================================
# 数据库配置（从 config.json 读取）
# ============================================================
def load_db_config():
    """从 config.json 加载数据库配置"""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    config_paths = [
        os.path.join(script_dir, "config.json"),
        os.path.join(os.path.dirname(script_dir), "config.json"),
        os.path.expanduser("~/.genealogy_config.json"),
        "config.json",
    ]
    for path in config_paths:
        if os.path.exists(path):
            with open(path, "r", encoding="utf-8") as f:
                config = json.load(f)
            db = config["db_clients"][0]
            return {
                "host": db.get("host", "localhost"),
                "port": db.get("port", 3306),
                "user": db.get("user", "root"),
                "password": db.get("password", "123456"),
                "database": db.get("dbname", "genealogy_db"),
                "charset": db.get("character_set", "utf8mb4"),
            }
    # 默认配置
    return {
        "host": "localhost", "port": 3306,
        "user": "root", "password": "123456",
        "database": "genealogy_db", "charset": "utf8mb4",
    }


class GenealogyExporter:
    def __init__(self, db_config=None):
        self.db_config = db_config or load_db_config()
        self.connection = None
        self.cursor = None

    def connect(self):
        self.connection = pymysql.connect(**self.db_config)
        self.cursor = self.connection.cursor(pymysql.cursors.DictCursor)

    def disconnect(self):
        if self.cursor:
            self.cursor.close()
        if self.connection:
            self.connection.close()

    # ============================================================
    # 全量导出：导出整个族谱
    # ============================================================
    def export_genealogy_full(self, genealogy_id, output_dir=None):
        """导出整个族谱的所有成员和婚姻"""
        self.connect()
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

        if output_dir is None:
            output_dir = os.path.join("exports", f"genealogy_{genealogy_id}_full_{timestamp}")
        os.makedirs(output_dir, exist_ok=True)

        try:
            # 1. 族谱信息
            self.cursor.execute(
                "SELECT * FROM Genealogy WHERE genealogy_id = %s",
                (genealogy_id,),
            )
            genealogy = self.cursor.fetchone()
            if not genealogy:
                print(f"错误: 族谱ID={genealogy_id} 不存在")
                return None

            with open(os.path.join(output_dir, "genealogy.csv"), "w", newline="", encoding="utf-8") as f:
                w = csv.writer(f)
                w.writerow(["genealogy_id", "name", "surname", "founder_name", "create_time", "description", "user_id"])
                w.writerow([genealogy.get(k, "") for k in
                    ["genealogy_id", "name", "surname", "founder_name", "create_time", "description", "user_id"]])

            # 2. 全部成员（不含 generation 列，匹配 import 格式）
            self.cursor.execute(
                """SELECT member_id, genealogy_id, name, gender, birth_date,
                          death_date, biography, father_id, mother_id, spouse_id
                   FROM Member WHERE genealogy_id = %s ORDER BY member_id""",
                (genealogy_id,),
            )
            members = self.cursor.fetchall()

            with open(os.path.join(output_dir, "member.csv"), "w", newline="", encoding="utf-8") as f:
                w = csv.writer(f)
                w.writerow(["member_id", "genealogy_id", "name", "gender", "birth_date",
                            "death_date", "biography", "father_id", "mother_id", "spouse_id"])
                for m in members:
                    w.writerow([m.get(k, "") for k in
                        ["member_id", "genealogy_id", "name", "gender", "birth_date",
                         "death_date", "biography", "father_id", "mother_id", "spouse_id"]])

            # 3. 相关婚姻
            member_ids = [m["member_id"] for m in members]
            marriages = []
            if member_ids:
                # 分批查询避免 IN 子句过长
                batch_size = 5000
                for i in range(0, len(member_ids), batch_size):
                    batch = member_ids[i : i + batch_size]
                    placeholders = ",".join(["%s"] * len(batch))
                    self.cursor.execute(
                        f"""SELECT * FROM Marriage
                            WHERE husband_id IN ({placeholders})
                               OR wife_id IN ({placeholders})
                            ORDER BY marriage_id""",
                        batch + batch,
                    )
                    marriages.extend(self.cursor.fetchall())

            with open(os.path.join(output_dir, "marriage.csv"), "w", newline="", encoding="utf-8") as f:
                w = csv.writer(f)
                w.writerow(["marriage_id", "husband_id", "wife_id", "wedding_date", "status", "created_at"])
                for m in marriages:
                    w.writerow([m.get(k, "") for k in
                        ["marriage_id", "husband_id", "wife_id", "wedding_date", "status", "created_at"]])

            # 4. 元信息
            with open(os.path.join(output_dir, "export_info.txt"), "w", encoding="utf-8") as f:
                f.write(f"# 家谱系统数据导出\n")
                f.write(f"导出时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
                f.write(f"导出模式: 全量导出\n")
                f.write(f"族谱ID: {genealogy_id}\n")
                f.write(f"族谱名称: {genealogy.get('name', '')}\n")
                f.write(f"成员数量: {len(members)}\n")
                f.write(f"婚姻记录: {len(marriages)}\n")

            print(f"✓ 全量导出完成: {output_dir}")
            print(f"  成员: {len(members)} 人, 婚姻: {len(marriages)} 条")
            return output_dir

        except Exception as e:
            print(f"导出失败: {e}")
            raise
        finally:
            self.disconnect()

    # ============================================================
    # 分支导出：导出指定成员 + 祖先 + 后代
    # ============================================================
    def export_genealogy_branch(self, genealogy_id, root_member_id, output_dir=None):
        """导出族谱分支：根成员 + 所有祖先 + 所有后代"""
        self.connect()
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

        if output_dir is None:
            output_dir = os.path.join(
                "exports", f"genealogy_{genealogy_id}_branch_{root_member_id}_{timestamp}"
            )
        os.makedirs(output_dir, exist_ok=True)

        try:
            # 验证根成员
            self.cursor.execute(
                "SELECT member_id, name FROM Member WHERE member_id = %s AND genealogy_id = %s",
                (root_member_id, genealogy_id),
            )
            root = self.cursor.fetchone()
            if not root:
                print(f"错误: 成员ID={root_member_id} 在族谱ID={genealogy_id} 中不存在")
                return None

            root_name = root["name"]
            print(f"根成员: {root_name} (ID={root_member_id})")

            # ============================================================
            # 关键修复：拆分为两个独立的单向递归 CTE
            # 原 bug：在同一个 CTE 中同时向上和向下搜索，导致循环引用
            # 修复：两个独立的 CTE，分别查祖先和后代，再合并
            # ============================================================

            # 1. 查找所有祖先（向上递归 CTE）
            print("查找祖先（向上递归）...")
            self.cursor.execute(
                """WITH RECURSIVE ancestors AS (
                       SELECT member_id, father_id, mother_id, 0 as depth
                       FROM Member
                       WHERE member_id = %s AND genealogy_id = %s
                       UNION ALL
                       SELECT m.member_id, m.father_id, m.mother_id, a.depth + 1
                       FROM Member m
                       JOIN ancestors a
                         ON (m.member_id = a.father_id OR m.member_id = a.mother_id)
                       WHERE m.genealogy_id = %s
                         AND ((a.father_id IS NOT NULL AND a.father_id > 0)
                           OR (a.mother_id IS NOT NULL AND a.mother_id > 0))
                         AND a.depth < 30
                   )
                   SELECT DISTINCT member_id FROM ancestors WHERE depth > 0""",
                (root_member_id, genealogy_id, genealogy_id),
            )
            ancestor_ids = set(row["member_id"] for row in self.cursor.fetchall())
            print(f"  祖先: {len(ancestor_ids)} 人")

            # 2. 查找所有后代（向下递归 CTE）
            print("查找后代（向下递归）...")
            self.cursor.execute(
                """WITH RECURSIVE descendants AS (
                       SELECT member_id, 0 as depth
                       FROM Member
                       WHERE member_id = %s AND genealogy_id = %s
                       UNION ALL
                       SELECT m.member_id, d.depth + 1
                       FROM Member m
                       JOIN descendants d
                         ON (m.father_id = d.member_id OR m.mother_id = d.member_id)
                       WHERE m.genealogy_id = %s
                         AND d.depth < 30
                   )
                   SELECT DISTINCT member_id FROM descendants WHERE depth > 0""",
                (root_member_id, genealogy_id, genealogy_id),
            )
            descendant_ids = set(row["member_id"] for row in self.cursor.fetchall())
            print(f"  后代: {len(descendant_ids)} 人")

            # 3. 合并
            all_member_ids = {root_member_id} | ancestor_ids | descendant_ids
            print(f"  分支总计: {len(all_member_ids)} 人")

            # 4. 导出族谱信息
            self.cursor.execute(
                "SELECT * FROM Genealogy WHERE genealogy_id = %s", (genealogy_id,)
            )
            genealogy = self.cursor.fetchone()
            with open(os.path.join(output_dir, "genealogy.csv"), "w", newline="", encoding="utf-8") as f:
                w = csv.writer(f)
                w.writerow(["genealogy_id", "name", "surname", "founder_name", "create_time", "description", "user_id"])
                w.writerow([genealogy.get(k, "") for k in
                    ["genealogy_id", "name", "surname", "founder_name", "create_time", "description", "user_id"]])

            # 5. 分批导出成员
            member_list = list(all_member_ids)
            batch_size = 5000
            all_members = []
            for i in range(0, len(member_list), batch_size):
                batch = member_list[i : i + batch_size]
                placeholders = ",".join(["%s"] * len(batch))
                self.cursor.execute(
                    f"""SELECT member_id, genealogy_id, name, gender, birth_date,
                               death_date, biography, father_id, mother_id, spouse_id
                        FROM Member
                        WHERE member_id IN ({placeholders})
                        ORDER BY member_id""",
                    batch,
                )
                all_members.extend(self.cursor.fetchall())

            with open(os.path.join(output_dir, "member.csv"), "w", newline="", encoding="utf-8") as f:
                w = csv.writer(f)
                w.writerow(["member_id", "genealogy_id", "name", "gender", "birth_date",
                            "death_date", "biography", "father_id", "mother_id", "spouse_id"])
                for m in all_members:
                    w.writerow([m.get(k, "") for k in
                        ["member_id", "genealogy_id", "name", "gender", "birth_date",
                         "death_date", "biography", "father_id", "mother_id", "spouse_id"]])

            # 6. 导出婚姻
            marriages = []
            for i in range(0, len(member_list), batch_size):
                batch = member_list[i : i + batch_size]
                placeholders = ",".join(["%s"] * len(batch))
                self.cursor.execute(
                    f"""SELECT * FROM Marriage
                        WHERE husband_id IN ({placeholders})
                           OR wife_id IN ({placeholders})
                        ORDER BY marriage_id""",
                    batch + batch,
                )
                marriages.extend(self.cursor.fetchall())

            with open(os.path.join(output_dir, "marriage.csv"), "w", newline="", encoding="utf-8") as f:
                w = csv.writer(f)
                w.writerow(["marriage_id", "husband_id", "wife_id", "wedding_date", "status", "created_at"])
                for m in marriages:
                    w.writerow([m.get(k, "") for k in
                        ["marriage_id", "husband_id", "wife_id", "wedding_date", "status", "created_at"]])

            # 7. 元信息
            with open(os.path.join(output_dir, "export_info.txt"), "w", encoding="utf-8") as f:
                f.write(f"# 家谱系统数据导出\n")
                f.write(f"导出时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
                f.write(f"导出模式: 分支导出\n")
                f.write(f"族谱ID: {genealogy_id}\n")
                f.write(f"族谱名称: {genealogy.get('name', '')}\n")
                f.write(f"根成员ID: {root_member_id}\n")
                f.write(f"根成员姓名: {root_name}\n")
                f.write(f"祖先数量: {len(ancestor_ids)}\n")
                f.write(f"后代数量: {len(descendant_ids)}\n")
                f.write(f"分支总人数: {len(all_member_ids)}\n")
                f.write(f"婚姻记录: {len(marriages)}\n")

            print(f"✓ 分支导出完成: {output_dir}")
            print(f"  祖先 {len(ancestor_ids)} 代 ← [{root_name}] → 后代 {len(descendant_ids)} 人")
            return output_dir

        except Exception as e:
            print(f"导出失败: {e}")
            raise
        finally:
            self.disconnect()


def main():
    if len(sys.argv) < 2:
        print("用法:")
        print("  python export_backup.py full <genealogy_id>           全量导出")
        print("  python export_backup.py branch <genealogy_id> <root_id>  分支导出")
        print("  python export_backup.py list                          列出所有族谱")
        sys.exit(1)

    mode = sys.argv[1]
    exporter = GenealogyExporter()

    try:
        if mode == "full":
            if len(sys.argv) < 3:
                print("请指定族谱ID: python export_backup.py full <genealogy_id>")
                sys.exit(1)
            gid = int(sys.argv[2])
            output_dir = exporter.export_genealogy_full(gid)

        elif mode == "branch":
            if len(sys.argv) < 4:
                print("请指定族谱ID和根成员ID: python export_backup.py branch <genealogy_id> <root_id>")
                sys.exit(1)
            gid = int(sys.argv[2])
            rid = int(sys.argv[3])
            output_dir = exporter.export_genealogy_branch(gid, rid)

        elif mode == "list":
            exporter.connect()
            exporter.cursor.execute(
                """SELECT g.genealogy_id, g.name, g.surname,
                          COUNT(DISTINCT m.member_id) AS member_count, g.create_time
                   FROM Genealogy g
                   LEFT JOIN Member m ON g.genealogy_id = m.genealogy_id
                   GROUP BY g.genealogy_id
                   ORDER BY g.genealogy_id"""
            )
            print(f"{'ID':<6} {'名称':<20} {'姓氏':<8} {'成员数':<8} {'创建时间'}")
            print("-" * 60)
            for row in exporter.cursor.fetchall():
                print(f"{row['genealogy_id']:<6} {row['name']:<20} {row['surname']:<8} {row['member_count']:<8} {str(row['create_time'])}")
            exporter.disconnect()
            return

        else:
            print(f"未知模式: {mode}")
            sys.exit(1)

        if output_dir and os.path.exists(output_dir):
            print(f"\n导出文件列表:")
            for f in os.listdir(output_dir):
                fpath = os.path.join(output_dir, f)
                size = os.path.getsize(fpath)
                print(f"  {f} ({size:,} bytes)")

    except Exception as e:
        print(f"导出失败: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()