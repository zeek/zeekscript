use std::string::FromUtf8Error;

use thiserror::Error;
use topiary_core::{FormatterError, TopiaryQuery};

#[derive(Error, Debug)]
pub enum FormatError {
    #[error("parse error")]
    Parse,

    #[error("internal query error")]
    Query(String),

    #[error("idempotency violated")]
    Idempotency,

    #[error("UTF8 conversion error")]
    UTF8(FromUtf8Error),

    #[error("unknown error")]
    Unknown,
}

const QUERY: &str = include_str!("query.scm");

pub fn format(
    input: &str,
    skip_idempotence: bool,
    tolerate_parsing_errors: bool,
) -> Result<String, FormatError> {
    let mut output = Vec::new();

    let grammar = topiary_tree_sitter_facade::Language::from(tree_sitter_zeek::LANGUAGE);

    let query = TopiaryQuery::new(&grammar, QUERY).map_err(|e| match e {
        FormatterError::Query(m, e) => FormatError::Query(match e {
            None => m,
            Some(e) => format!("{m}: {e}"),
        }),
        _ => FormatError::Unknown,
    })?;

    let language = topiary_core::Language {
        name: "zeek".to_string(),
        indent: Some("\t".into()),
        grammar,
        query,
    };

    if let Err(e) = topiary_core::formatter(
        &mut input.as_bytes(),
        &mut output,
        &language,
        topiary_core::Operation::Format {
            skip_idempotence,
            tolerate_parsing_errors,
        },
    ) {
        Err(match e {
            FormatterError::Query(m, e) => FormatError::Query(match e {
                None => m,
                Some(e) => format!("{m}: {e}"),
            }),
            FormatterError::Idempotence => FormatError::Idempotency,
            FormatterError::Parsing { .. } => FormatError::Parse,
            _ => FormatError::Unknown,
        })?;
    };

    let output = String::from_utf8(output).map_err(FormatError::UTF8)?;

    Ok(output)
}

#[cfg(feature = "python")]
#[pyo3::pymodule]
mod zeekscript {
    use pyo3::{exceptions::PyException, pyfunction, PyResult};

    #[pyfunction]
    fn format(input: &str) -> PyResult<String> {
        super::format(input, false, true).map_err(|e| PyException::new_err(e.to_string()))
    }
}

#[cfg(test)]
mod test {
    use insta::assert_debug_snapshot;

    use crate::FormatError;

    fn format(input: &str) -> Result<String, FormatError> {
        crate::format(input, false, false)
    }

    #[test]
    fn comments() {
        assert_debug_snapshot!(format("# foo\n;1;"));
        assert_debug_snapshot!(format("##! foo\n;1;"));
        assert_debug_snapshot!(format("## foo\n1;"));
        assert_debug_snapshot!(format("##< foo\n1;"));

        assert_debug_snapshot!(format("1;# foo\n;1;"));
        assert_debug_snapshot!(format("1;##! foo\n;1;"));
        assert_debug_snapshot!(format("1;## foo\n1;"));
        assert_debug_snapshot!(format("1;##< foo"));
        assert_debug_snapshot!(format("1;##< foo\n##< bar"));
    }
}
