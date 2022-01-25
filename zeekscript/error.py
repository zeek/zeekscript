"""Exception hierarchy for the zeekscript package."""

class Error(Exception):
    """Base class for all zeekscript errors."""

class FileError(Error):
    """System errors while processing script files"""

class ParserError(Error):
    """Errors while parsing a script"""
    def __init__(self, message=None, line=None):
        """A message describing the error, and the full line it occurred on,
        for context."""
        super().__init__(message)
        self.line = line
