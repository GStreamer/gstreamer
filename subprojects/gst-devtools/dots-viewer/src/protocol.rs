use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize)]
pub enum IncomingMessageType {
    Hello,
    Snapshot,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct IncomingMessage {
    #[serde(rename = "type")]
    pub type_: IncomingMessageType,
}
