"""Exception hierarchy for the zeekscript package."""


class Error(Exception):
    """Base class for all zeekscript errors."""


class FileError(Error):
    """System errors while processing script files"""


class ParserError(Error):
    """A hard parsing error, producing no parse tree."""
