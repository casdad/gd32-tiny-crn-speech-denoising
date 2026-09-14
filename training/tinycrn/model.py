import torch
import torch.nn as nn


class TinyCRN(nn.Module):
    """MCU-friendly Tiny-CRN.

    Input shape:  [B, T, F, 1]  (NHWC-like, friendly for TFLite/GD32)
    Output shape: [B, F]        (current-frame band mask)
    """

    def __init__(self, n_bands=32, context_frames=8, conv_channels=8,
                 pointwise_channels=16, lstm_hidden=32, lstm_layers=1, dropout=0.0):
        super().__init__()
        self.n_bands = int(n_bands)
        self.context_frames = int(context_frames)
        self.conv = nn.Sequential(
            nn.Conv2d(1, conv_channels, kernel_size=3, padding=1, bias=True),
            nn.ReLU(inplace=True),
            nn.Conv2d(conv_channels, conv_channels, kernel_size=3, padding=1,
                      groups=conv_channels, bias=True),
            nn.ReLU(inplace=True),
            nn.Conv2d(conv_channels, pointwise_channels, kernel_size=1, bias=True),
            nn.ReLU(inplace=True),
        )
        self.lstm = nn.LSTM(
            input_size=pointwise_channels * n_bands,
            hidden_size=lstm_hidden,
            num_layers=lstm_layers,
            batch_first=True,
            dropout=dropout if lstm_layers > 1 else 0.0,
        )
        self.fc = nn.Linear(lstm_hidden, n_bands)
        self.out_act = nn.Sigmoid()

    def forward(self, x):
        # x: [B,T,F,1]
        if x.dim() != 4:
            raise ValueError("TinyCRN input must be [B,T,F,1]")
        x = x.permute(0, 3, 1, 2).contiguous()  # [B,1,T,F]
        y = self.conv(x)                         # [B,C,T,F]
        y = y.permute(0, 2, 1, 3).contiguous()   # [B,T,C,F]
        b, t, c, f = y.shape
        y = y.view(b, t, c * f)
        y, _ = self.lstm(y)
        y = y[:, -1, :]
        return self.out_act(self.fc(y))


def build_model_from_config(cfg: dict) -> TinyCRN:
    mcfg = cfg.get("model", {})
    return TinyCRN(
        n_bands=cfg.get("n_bands", 32),
        context_frames=cfg.get("context_frames", 8),
        conv_channels=mcfg.get("conv_channels", 8),
        pointwise_channels=mcfg.get("pointwise_channels", 16),
        lstm_hidden=mcfg.get("lstm_hidden", 32),
        lstm_layers=mcfg.get("lstm_layers", 1),
        dropout=mcfg.get("dropout", 0.0),
    )
