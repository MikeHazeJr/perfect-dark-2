"""
Time formatting helpers for the daily-flow orchestrator.

Mike's preferred display format: [mm-dd-yyyy - hh:mm] in 24-hour Eastern time,
no timezone tag.

For internal state (last-run.json, briefing generated_at) we use ISO-8601 with
the ET offset so downstream tools can parse unambiguously.
"""

from __future__ import annotations

import datetime as dt

LOG_PREFIX = "DAILY-FLOW.TIMEFMT"


def _second_sunday(year: int, month: int) -> dt.date:
    d = dt.date(year, month, 1)
    while d.weekday() != 6:
        d += dt.timedelta(days=1)
    return d + dt.timedelta(days=7)


def _first_sunday(year: int, month: int) -> dt.date:
    d = dt.date(year, month, 1)
    while d.weekday() != 6:
        d += dt.timedelta(days=1)
    return d


class _EasternTime(dt.tzinfo):
    """America/New_York implementation that does not depend on IANA tzdata.

    Standard time (EST, UTC-5) runs from the first Sunday of November at 02:00 local
    to the second Sunday of March at 02:00 local. Daylight time (EDT, UTC-4) is the
    rest of the year. Rules established by Energy Policy Act of 2005 and stable since.
    """

    def utcoffset(self, when: dt.datetime | None) -> dt.timedelta:
        return dt.timedelta(hours=-4) if self._is_dst(when) else dt.timedelta(hours=-5)

    def dst(self, when: dt.datetime | None) -> dt.timedelta:
        return dt.timedelta(hours=1) if self._is_dst(when) else dt.timedelta(0)

    def tzname(self, when: dt.datetime | None) -> str:
        return "EDT" if self._is_dst(when) else "EST"

    def _is_dst(self, when: dt.datetime | None) -> bool:
        if when is None:
            return False
        year = when.year
        dst_start = dt.datetime.combine(_second_sunday(year, 3), dt.time(2, 0))
        dst_end = dt.datetime.combine(_first_sunday(year, 11), dt.time(2, 0))
        naive = when.replace(tzinfo=None) if when.tzinfo else when
        return dst_start <= naive < dst_end


ET = _EasternTime()


def now_et() -> dt.datetime:
    return dt.datetime.now(tz=ET)


def today_et() -> dt.date:
    return now_et().date()


def format_display(ts: dt.datetime | None = None) -> str:
    """Returns Mike's preferred timestamp format [mm-dd-yyyy - hh:mm]."""
    t = ts if ts is not None else now_et()
    if t.tzinfo is None:
        t = t.replace(tzinfo=ET)
    else:
        t = t.astimezone(ET)
    return f"[{t.strftime('%m-%d-%Y')} - {t.strftime('%H:%M')}]"


def format_iso_et(ts: dt.datetime | None = None) -> str:
    t = ts if ts is not None else now_et()
    if t.tzinfo is None:
        t = t.replace(tzinfo=ET)
    else:
        t = t.astimezone(ET)
    return t.isoformat()


def iso_window_24h(today: dt.date | None = None) -> tuple[str, str]:
    """Returns (start_iso, end_iso) for the 24-hour window ending at start-of-day today ET."""
    d = today if today is not None else today_et()
    start = dt.datetime.combine(d - dt.timedelta(days=1), dt.time(0, 0), tzinfo=ET)
    end = dt.datetime.combine(d, dt.time(0, 0), tzinfo=ET)
    return start.isoformat(), end.isoformat()


def is_monday(today: dt.date | None = None) -> bool:
    return (today if today is not None else today_et()).weekday() == 0


def is_first_monday_of_month(today: dt.date | None = None) -> bool:
    d = today if today is not None else today_et()
    return d.weekday() == 0 and d.day <= 7


def iso_week_label(today: dt.date | None = None) -> str:
    """Returns YYYY-WNN per ISO 8601."""
    d = today if today is not None else today_et()
    iso = d.isocalendar()
    return f"{iso.year}-W{iso.week:02d}"


def month_label(today: dt.date | None = None) -> str:
    """Returns YYYY-MM for the calendar month being summarized.

    Called on the first Monday of a month, this returns the PRIOR month's label."""
    d = today if today is not None else today_et()
    prior = d.replace(day=1) - dt.timedelta(days=1)
    return f"{prior.year}-{prior.month:02d}"


def week_window(today: dt.date | None = None) -> tuple[dt.date, dt.date]:
    """Returns (mon, sun) for the prior ISO week ending the Sunday before today."""
    d = today if today is not None else today_et()
    sun = d - dt.timedelta(days=d.weekday() + 1)
    mon = sun - dt.timedelta(days=6)
    return mon, sun
