#!/usr/bin/env python3
"""Build a compact lunar-date table from HKO's official daily text tables."""

import argparse
from datetime import date, timedelta
import os
from pathlib import Path
import re
import sys
import time

import requests


SOURCE_INDEX_URL = "https://www.hko.gov.hk/tc/gts/time/conversion1_text.htm"
SOURCE_YEAR_URL = "https://www.hko.gov.hk/tc/gts/time/calendar/text/files/T{year}c.txt"
SOURCE_HOST = "www.hko.gov.hk"
START_YEAR = 1901
END_YEAR = 2100
MAGIC = b"HKL1"
HEADER_SIZE = 10
RECORD_SIZE = 3

STEMS = ("甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸")
BRANCHES = ("子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥")
MONTHS = {
    "正月": 1,
    "一月": 1,
    "二月": 2,
    "三月": 3,
    "四月": 4,
    "五月": 5,
    "六月": 6,
    "七月": 7,
    "八月": 8,
    "九月": 9,
    "十月": 10,
    "十一月": 11,
    "十二月": 12,
}
DAY_NAMES = {"初" + digit: number for number, digit in enumerate(("一", "二", "三", "四", "五", "六", "七", "八", "九", "十"), 1)}
DAY_NAMES.update({"十" + digit: 10 + number for number, digit in enumerate(("一", "二", "三", "四", "五", "六", "七", "八", "九"), 1)})
DAY_NAMES["二十"] = 20
DAY_NAMES.update({"廿" + digit: 20 + number for number, digit in enumerate(("一", "二", "三", "四", "五", "六", "七", "八", "九"), 1)})
DAY_NAMES["三十"] = 30

HEADER_RE = re.compile(r"^\ufeff?\s*(\d{4})\s*[（(]\s*([甲乙丙丁戊己庚辛壬癸][子丑寅卯辰巳午未申酉戌亥])")
DATE_RE = re.compile(r"^(\d{4})年(\d{1,2})月(\d{1,2})日$")


def sexagenary_index(name):
    """Return the 0-based sexagenary-cycle index for a stem-branch pair."""
    for index in range(60):
        if STEMS[index % 10] + BRANCHES[index % 12] == name:
            return index
    raise ValueError("HKO 年标题中的干支无效: {!r}".format(name))


def parse_lunar_token(token):
    """Parse one HKO lunar token into ('month', month, leap) or ('day', day)."""
    leap = token.startswith("閏")
    month_token = token[1:] if leap else token
    if month_token in MONTHS:
        return "month", MONTHS[month_token], leap
    if leap:
        raise ValueError("HKO 闰月名称无法解析: {!r}".format(token))
    if token in DAY_NAMES:
        return "day", DAY_NAMES[token], False
    raise ValueError("HKO 农历日名称无法解析: {!r}".format(token))


def parse_year_text(text, expected_year):
    """Parse and validate one official HKO year text into ordered day rows."""
    header = None
    rows = []
    for line in text.splitlines():
        stripped = line.strip()
        if header is None:
            match = HEADER_RE.match(stripped)
            if match:
                header = (int(match.group(1)), sexagenary_index(match.group(2)))
        parts = stripped.split()
        if len(parts) < 2:
            continue
        date_match = DATE_RE.match(parts[0])
        if not date_match:
            continue
        row_year, month, day = (int(value) for value in date_match.groups())
        if row_year != expected_year:
            raise ValueError("HKO 日期年份不匹配: {} != {}".format(row_year, expected_year))
        token_type, first, second = parse_lunar_token(parts[1])
        rows.append((date(row_year, month, day), token_type, first, second))

    if header is None or header[0] != expected_year:
        raise ValueError("HKO 文件标题缺失或年份不匹配: {}".format(expected_year))
    expected_count = 366 if date(expected_year, 12, 31).timetuple().tm_yday == 366 else 365
    if len(rows) != expected_count:
        raise ValueError(
            "HKO {} 年应有 {} 天记录，实际 {} 天".format(expected_year, expected_count, len(rows))
        )
    first_date = date(expected_year, 1, 1)
    for index, row in enumerate(rows):
        if row[0] != first_date + timedelta(days=index):
            raise ValueError("HKO {} 年存在日期缺失、重复或乱序".format(expected_year))
    return header[1], rows


def infer_initial_month(rows):
    """Infer the lunar month covering 1901-01-01 from its next month marker."""
    if not rows:
        raise ValueError("首年 HKO 数据为空")
    first_kind, first_value, first_leap = rows[0][1:]
    if first_kind == "month":
        return first_value, first_leap

    for _, kind, value, leap in rows[1:]:
        if kind != "month":
            continue
        if leap:
            return value, False
        if value == 1:
            return 12, False
        return value - 1, False
    raise ValueError("首年 HKO 数据中找不到月份切换，无法初始化年表")


def _is_valid_next_month(previous_month, previous_leap, month, leap):
    if previous_leap:
        return not leap and month == (previous_month % 12) + 1
    if leap:
        return month == previous_month
    return month == (previous_month % 12) + 1


def encode_years(year_data, start_year=START_YEAR, end_year=END_YEAR):
    """Validate contiguous lunar dates and encode three bytes per day."""
    if start_year != START_YEAR or end_year != END_YEAR:
        raise ValueError("当前紧凑表格式要求完整覆盖 1901–2100 年")
    if len(year_data) != end_year - start_year + 1:
        raise ValueError("HKO 年份数据不完整")

    initial_month, initial_leap = infer_initial_month(year_data[0][1])
    current_month = initial_month
    current_leap = initial_leap
    current_day = None
    records = bytearray()
    previous_header_cycle = None

    for year_offset, (header_cycle, rows) in enumerate(year_data):
        gregorian_year = start_year + year_offset
        if previous_header_cycle is not None and header_cycle != (previous_header_cycle + 1) % 60:
            raise ValueError("HKO 年标题干支序列不连续: {} 年".format(gregorian_year))
        previous_header_cycle = header_cycle

        new_year_seen = False
        new_year_count = 0
        for current_date, token_type, value, leap in rows:
            if token_type == "month":
                month = value
                if current_day is not None and current_day not in (29, 30):
                    raise ValueError("农历月份长度不是 29 或 30 天: {}".format(current_date))
                if current_day is not None and not _is_valid_next_month(
                    current_month, current_leap, month, leap
                ):
                    raise ValueError("农历月份顺序错误: {}".format(current_date))
                if month == 1 and not leap:
                    new_year_seen = True
                    new_year_count += 1
                current_month = month
                current_leap = leap
                current_day = 1
            else:
                day = value
                if current_day is None:
                    current_day = day
                elif day != current_day + 1:
                    raise ValueError("农历日期序号不连续: {}".format(current_date))
                if day < 1 or day > 30:
                    raise ValueError("农历日序号越界: {}".format(current_date))
                current_day = day

            cycle_index = header_cycle if new_year_seen else (header_cycle - 1) % 60
            month_flags = current_month | (0x80 if current_leap else 0)
            records.extend((cycle_index, month_flags, current_day))

        if new_year_count != 1:
            raise ValueError("HKO {} 年应恰有一个正月初一，实际 {} 个".format(gregorian_year, new_year_count))

    expected_count = 73049
    if len(records) != expected_count * RECORD_SIZE:
        raise ValueError("年表日数错误: {}".format(len(records) // RECORD_SIZE))
    return records


def download_year(session, year):
    url = SOURCE_YEAR_URL.format(year=year)
    last_error = None
    for attempt in range(3):
        try:
            response = session.get(url, timeout=(5, 20))
            response.raise_for_status()
            final_host = requests.utils.urlparse(response.url).hostname
            if final_host != SOURCE_HOST:
                raise ValueError("HKO 请求重定向到了非官方主机: {}".format(final_host))
            return response.content.decode("utf-8-sig")
        except (requests.RequestException, UnicodeDecodeError, ValueError) as exc:
            last_error = exc
            if attempt < 2:
                time.sleep(0.25 * (attempt + 1))
    raise RuntimeError("下载 HKO {} 年数据失败: {}".format(year, last_error))


def build_table(output_path, session=None):
    """Download official HKO daily text and write the validated binary table."""
    owns_session = session is None
    if owns_session:
        session = requests.Session()
    try:
        year_data = []
        for year in range(START_YEAR, END_YEAR + 1):
            text = download_year(session, year)
            year_data.append(parse_year_text(text, year))
            if (year - START_YEAR + 1) % 25 == 0:
                print("已校验 {} / {} 年".format(year - START_YEAR + 1, END_YEAR - START_YEAR + 1))
        records = encode_years(year_data)
    finally:
        if owns_session:
            session.close()

    header = bytearray(MAGIC)
    header.extend((START_YEAR & 0xFF, (START_YEAR >> 8) & 0xFF))
    count = len(records) // RECORD_SIZE
    header.extend((count & 0xFF, (count >> 8) & 0xFF, (count >> 16) & 0xFF, (count >> 24) & 0xFF))

    output = Path(output_path)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + ".tmp")
    try:
        with temporary.open("wb") as target:
            target.write(header)
            target.write(records)
        os.replace(str(temporary), str(output))
    except Exception:
        try:
            temporary.unlink()
        except OSError:
            pass
        raise
    return count, len(header) + len(records)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--start", type=int, default=START_YEAR)
    parser.add_argument("--end", type=int, default=END_YEAR)
    parser.add_argument("--output", default="pico/lunar_table.bin")
    args = parser.parse_args(argv)
    if args.start != START_YEAR or args.end != END_YEAR:
        parser.error("年表必须完整覆盖 1901–2100 年")

    print("数据来源：{}".format(SOURCE_INDEX_URL))
    try:
        count, size = build_table(args.output)
    except Exception as exc:
        print("农历年表构建失败：{}".format(exc), file=sys.stderr)
        return 1
    print("已写入 {:,} 天，文件大小 {:,} 字节：{}".format(count, size, args.output))
    print("注意：HKO 提示 2057、2089、2097 年的部分天文交界日期存在不确定性；本地表遵循其公布版本。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
