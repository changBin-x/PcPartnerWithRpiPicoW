"""Compact Hong Kong Observatory lunar-date lookup for MicroPython."""

_MAGIC = b"HKL1"
_HEADER_SIZE = 10
_RECORD_SIZE = 3
_START_YEAR_FALLBACK = 1901

_STEMS = ("甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸")
_BRANCHES = ("子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥")
_MONTH_NAMES = (
    "",
    "正月",
    "二月",
    "三月",
    "四月",
    "五月",
    "六月",
    "七月",
    "八月",
    "九月",
    "十月",
    "冬月",
    "腊月",
)
_DIGITS = ("零", "一", "二", "三", "四", "五", "六", "七", "八", "九")
_DAYS_PER_MONTH = (31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)


def _default_table_path():
    module_file = globals().get("__file__", "")
    separator = "/" if "/" in module_file else "\\" if "\\" in module_file else ""
    if separator:
        module_dir = module_file.rsplit(separator, 1)[0]
        if module_dir:
            return module_dir + separator + "lunar_table.bin"
    return "lunar_table.bin"


def _date_parts(value):
    try:
        year = value.year
        month = value.month
        day = value.day
    except AttributeError:
        try:
            year, month, day = value[0], value[1], value[2]
        except (TypeError, IndexError, KeyError):
            return None
    if any(isinstance(part, bool) or not isinstance(part, int) for part in (year, month, day)):
        return None
    return year, month, day


def _is_leap_year(year):
    return year % 4 == 0 and (year % 100 != 0 or year % 400 == 0)


def _days_in_month(year, month):
    if month == 2 and _is_leap_year(year):
        return 29
    return _DAYS_PER_MONTH[month - 1]


def _day_index(year, month, day, start_year):
    if year < start_year or month < 1 or month > 12:
        return None
    if day < 1 or day > _days_in_month(year, month):
        return None

    previous_year = year - 1
    start_previous_year = start_year - 1
    days = 365 * (year - start_year)
    days += previous_year // 4 - start_previous_year // 4
    days -= previous_year // 100 - start_previous_year // 100
    days += previous_year // 400 - start_previous_year // 400
    for month_number in range(1, month):
        days += _days_in_month(year, month_number)
    return days + day - 1


def lookup_lunar_date(date_value, table_path=None):
    """Return (sexagenary index, month, leap flag, day), or None outside table."""
    parts = _date_parts(date_value)
    if parts is None:
        return None
    year, month, day = parts

    path = _default_table_path() if table_path is None else table_path
    source = open(path, "rb")
    try:
        header = source.read(_HEADER_SIZE)
        if len(header) != _HEADER_SIZE or header[:4] != _MAGIC:
            raise ValueError("农历年表文件头无效")
        start_year = header[4] | (header[5] << 8)
        record_count = (
            header[6]
            | (header[7] << 8)
            | (header[8] << 16)
            | (header[9] << 24)
        )
        if not start_year or not record_count:
            raise ValueError("农历年表元数据无效")

        index = _day_index(year, month, day, start_year)
        if index is None or index >= record_count:
            return None
        source.seek(_HEADER_SIZE + index * _RECORD_SIZE)
        record = source.read(_RECORD_SIZE)
    finally:
        source.close()

    if len(record) != _RECORD_SIZE:
        raise ValueError("农历年表记录不完整")
    sexagenary_index = record[0]
    month_flags = record[1]
    lunar_month = month_flags & 0x0F
    is_leap_month = bool(month_flags & 0x80)
    lunar_day = record[2]
    if (
        sexagenary_index >= 60
        or month_flags & 0x70
        or lunar_month < 1
        or lunar_month > 12
        or lunar_day < 1
        or lunar_day > 30
    ):
        raise ValueError("农历年表记录超出有效范围")
    return sexagenary_index, lunar_month, is_leap_month, lunar_day


def _day_name(day):
    if day <= 10:
        return "初" + _DIGITS[day]
    if day < 20:
        return "十" + _DIGITS[day - 10]
    if day == 20:
        return "二十"
    if day < 30:
        return "廿" + _DIGITS[day - 20]
    return "三十"


def format_lunar_date(date_value, table_path=None):
    """Format a datetime.date or MicroPython localtime tuple in Simplified Chinese."""
    lunar_date = lookup_lunar_date(date_value, table_path)
    if lunar_date is None:
        return None
    sexagenary_index, month, is_leap_month, day = lunar_date
    year_name = _STEMS[sexagenary_index % 10] + _BRANCHES[sexagenary_index % 12]
    month_name = _MONTH_NAMES[month]
    if is_leap_month:
        month_name = "闰" + month_name
    return year_name + "年" + month_name + _day_name(day)
