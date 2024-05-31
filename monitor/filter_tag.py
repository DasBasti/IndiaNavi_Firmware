
import io
import fnmatch
from platformio.device.monitor.filters.base import DeviceMonitorFilterBase


class TagFilter(DeviceMonitorFilterBase):
    NAME = "tag_filter"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._line_started = False
        self._text = ""
        self._filter_fp = io.open("monitor.filter", "r", encoding="utf-8")
        self._contains = []
        for filter in self._filter_fp.readlines():
            self._contains.append(filter.strip())
            print("Filter log for: %s" % filter.strip())

    def rx(self, text):
        self._text += text
        if "\n" in self._text:
            split_text = self._text.split("\n")
            text = split_text[0]
            self._text = "\n".join(split_text[1:])
            if len(self._contains) == 0:
                return text + "\n"
            for f in self._contains:
                if fnmatch.fnmatch(text, f):
                    return text + "\n"
        return ""
