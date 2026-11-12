use windows_core::GUID;

pub(crate) const SERVICE_ID: GUID = GUID::from_u128(0x1a746212_84db_4acf_88b3_caaa26352774);

pub(crate) struct Profile {
    pub(crate) id: GUID,
    pub(crate) language: u16,
    pub(crate) layout: &'static str,
    pub(crate) description: &'static str,
    pub(crate) mode: u32,
}

pub(crate) const PROFILES: [Profile; 2] = [
    Profile {
        id: GUID::from_u128(0x0d52a994_9eb5_44ab_b18f_603dc5f69671),
        language: 0x0409,
        layout: "00000409",
        description: "Keyrill proof EN",
        mode: 1,
    },
    Profile {
        id: GUID::from_u128(0x4c1e583c_f8e5_4b7d_b66c_b7e7319aed51),
        language: 0x0419,
        layout: "00000419",
        description: "Keyrill proof RU",
        mode: 2,
    },
];
