"""Dict-style CJK glyph table backed by a fixed-width file in flash.

Keeping the 341 CJK bitmaps in RAM costs about 61 KB on the Pico W, which
leaves too little heap for the weather worker's TLS handshake. The bitmaps
stay in ``font_cn.bin`` instead and are read on demand with a small cache.
"""

DEFAULT_CACHE_LIMIT = 64


def default_path():
    """Resolve ``font_cn.bin`` next to this module, like ``lunar.py`` does."""
    module_file = globals().get("__file__", "")
    separator = "/" if "/" in module_file else "\\" if "\\" in module_file else ""
    if separator:
        module_dir = module_file.rsplit(separator, 1)[0]
        if module_dir:
            return module_dir + separator + "font_cn.bin"
    return "font_cn.bin"


class FlashGlyphs:
    """Read packed glyph bitmaps from flash, caching the most recent ones."""

    def __init__(self, path, order, bytes_per_glyph, cache_limit=DEFAULT_CACHE_LIMIT):
        self.path = path
        self.order = order
        self.bytes_per_glyph = bytes_per_glyph
        self.cache_limit = cache_limit
        self._cache = {}
        self._source = None

    def __len__(self):
        return len(self.order)

    def __contains__(self, character):
        return self.order.find(character) >= 0

    def __getitem__(self, character):
        glyph = self.get(character)
        if glyph is None:
            raise KeyError(character)
        return glyph

    def get(self, character, default=None):
        """Return the packed bitmap for one character, or ``default``."""
        glyph = self._cache.get(character)
        if glyph is not None:
            return glyph
        index = self.order.find(character)
        if index < 0:
            return default
        glyph = self._read(index)
        if glyph is None:
            return default
        if len(self._cache) >= self.cache_limit:
            self._cache.clear()
        self._cache[character] = glyph
        return glyph

    def _read(self, index):
        try:
            source = self._source
            if source is None:
                source = open(self.path, "rb")
                self._source = source
            source.seek(index * self.bytes_per_glyph)
            data = source.read(self.bytes_per_glyph)
        except OSError:
            self._source = None
            return None
        if data is None or len(data) != self.bytes_per_glyph:
            return None
        return data
