// src/engines/bibtex.rs -- Rustic interface to the bibtex processor.
// Copyright 2017 the Tectonic Project
// Licensed under the MIT License.

use std::collections::HashMap;
use std::ffi::{CStr, CString, OsString};

use errors::{ErrorKind, Result};
use io::IoStack;
use status::StatusBackend;
use super::{assign_global_state, c_api, ExecutionState, FileSummary};
use super::tex::TexResult;


pub struct BibtexEngine {
}


impl BibtexEngine {
    pub fn new () -> BibtexEngine {
        BibtexEngine {}
    }

    pub fn process (&mut self, io: &mut IoStack,
                    summaries: Option<&mut HashMap<OsString, FileSummary>>,
                    status: &mut StatusBackend, aux: &str) -> Result<TexResult> {
        let caux = CString::new(aux)?;

        let mut state = ExecutionState::new(io, summaries, status);

        unsafe {
            assign_global_state (&mut state, || {
                match c_api::bibtex_simple_main(caux.as_ptr()) {
                    0 => Ok(TexResult::Spotless),
                    1 => Ok(TexResult::Warnings),
                    2 => Ok(TexResult::Errors),
                    3 => {
                        Err(ErrorKind::Msg("unspecified fatal bibtex error".into()).into())
                    },
                    99 => {
                        let ptr = c_api::tt_get_error_message();
                        let msg = CStr::from_ptr(ptr).to_string_lossy().into_owned();
                        Err(ErrorKind::Msg(msg).into())
                    },
                    x => Err(ErrorKind::Msg(format!("internal error: unexpected 'history' value {}", x)).into())
                }
            })
        }
    }
}