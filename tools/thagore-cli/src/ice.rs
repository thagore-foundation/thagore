//! Internal compiler error handling for the Thagore CLI.

use std::io;
use std::panic::{self, AssertUnwindSafe};

use termcolor::{Color, ColorChoice, ColorSpec, StandardStream, WriteColor};

/// Exit code returned for internal compiler errors.
pub(crate) const ICE_EXIT_CODE: i32 = 2;

/// Runs `action` behind a panic boundary and converts panics into a stable ICE
/// exit code.
pub(crate) fn with_ice_handler<F>(action: F) -> i32
where
    F: FnOnce() -> i32,
{
    match panic::catch_unwind(AssertUnwindSafe(action)) {
        Ok(code) => code,
        Err(_) => {
            let _ = emit_ice(StandardStream::stderr(ColorChoice::Auto));
            ICE_EXIT_CODE
        }
    }
}

/// Emits the standard internal compiler error report prompt.
pub(crate) fn emit_ice<W>(mut writer: W) -> io::Result<()>
where
    W: WriteColor,
{
    writer.set_color(ColorSpec::new().set_fg(Some(Color::Red)).set_bold(true))?;
    writeln!(writer, "internal compiler error: thread panicked")?;
    writer.reset()?;
    writeln!(
        writer,
        "please report this at https://github.com/vietrix/thagore/issues"
    )?;
    writeln!(
        writer,
        "include: thagore version, OS, and the source file that triggered this"
    )
}

#[cfg(test)]
mod tests {
    use super::{emit_ice, with_ice_handler, ICE_EXIT_CODE};
    use termcolor::Buffer;

    #[test]
    fn panic_boundary_returns_ice_exit_code() {
        let code = with_ice_handler(|| panic!("boom"));
        assert_eq!(code, ICE_EXIT_CODE);
    }

    #[test]
    fn ice_report_mentions_bug_tracker() {
        let mut buffer = Buffer::ansi();
        emit_ice(&mut buffer).expect("emit ice");
        let rendered = String::from_utf8(buffer.into_inner()).expect("utf8");

        assert!(rendered.contains("internal compiler error"));
        assert!(rendered.contains("github.com/vietrix/thagore/issues"));
    }
}
