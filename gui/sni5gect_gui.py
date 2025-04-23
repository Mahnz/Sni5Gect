#!/usr/bin/env python3

import os
import time
import subprocess
import zmq
import zmq.asyncio
import asyncio
import numpy as np
import configparser
from shutil import copy
from pprint import pprint
from PIL import Image, ImageDraw
from nicegui import ui, app
from ansi2html import Ansi2HTMLConverter
from gui_logger import Log
from scipy.signal import ShortTimeFFT
from custom_colormap import parula_map
from time import sleep

CMD_ENABLE_AIRPLANE_MODE = "adb shell cmd connectivity airplane-mode enable"
CMD_DISABLE_AIRPLANE_MODE = "adb shell cmd connectivity airplane-mode disable"
CMD_NETWORK_STATUS = "adb shell dumpsys telephony.registry | grep mTelephonyDisplayInfo=TelephonyDisplayInfo"
CMD_PHONE_GET_INFO = 'adb shell getprop | grep "model\\|version.sdk\\|manufacturer\\|hardware\\|platform\\|revision\\|serialno\\|product.name\\|brand"'

UI_LEFT_CTRL_PANEL = None  # type: ui.row
UI_LEFT_PANEL = None  # type: ui.row
UI_ROW_UPPER = None  # type: ui.row
UI_ROW_BOTTOM = None  # type: ui.row
UI_RIGHT_PANEL = None  # type: ui.row

SNI5GECT_PATH = "/root/Sni5Gect"
SNI5GECT_MODULES_PATH = f"{SNI5GECT_PATH}/shadower/modules"
SNI5GECT_CFG_PATH = f"{SNI5GECT_PATH}/configs"
SNI5GECT_BIN_PATH = f"{SNI5GECT_PATH}/build/shadower/shadower"

snif5gect_proc = None  # type: subprocess.Popen
logger = None  # type: Log
sequence_diagram = None  # type: ui.mermaid

# list file names in the directory
module_files = os.listdir(SNI5GECT_MODULES_PATH)
module_files = [f for f in module_files if f.endswith(".cc")]
module_files = [f.replace(".cc", "") for f in module_files]
dummy_file_idx = module_files.index("dummy")

config_files = os.listdir(SNI5GECT_CFG_PATH)
config_files = [f for f in config_files if f.endswith(".conf")]
default_config_idx = config_files.index("config-srsran-n41-20MHz.conf")

conv = Ansi2HTMLConverter()
config = configparser.ConfigParser()


def calculate_spectogram(
    buf: bytes,
    sample_rate: int,
    fft_len: int,
    image_norm_factor: int,
):
    # One frame always spans 10ms in the signal
    # frame_size = int(sample_rate * 10 / 1000)
    num_samples_subframe = int(sample_rate * 1 / 1000)  # subframe always 1ms

    sample_size = 8  # fc32
    sf_size = int(sample_rate / 1000 * sample_size)
    image_num_subframe = int(len(buf) / sf_size)  # Number of subframes to show

    buf = buf[: image_num_subframe * sf_size]
    samples = np.frombuffer(buf, dtype=np.complex64)

    # print(samples)

    SFT = ShortTimeFFT(
        np.array([1.0] * fft_len),
        hop=fft_len,
        fs=sample_rate,
        mfft=fft_len,
        scale_to="psd",
        fft_mode="centered",
    )
    Sx = SFT.stft(samples, padding="zeros")  # perform the STFT

    Sx_norm = np.abs(Sx)
    sx_min = np.min(Sx_norm)
    sx_max = np.max(Sx_norm)

    # When the spectrogram only contains noise, image would be very bright after normalization
    if (sx_max - sx_min) / sx_min < 10000:
        sx_max *= 10

    # A min-max normalization
    Sx_norm = (Sx_norm - sx_min) * image_norm_factor / (sx_max - sx_min)
    Sx_norm = np.flip(Sx_norm, 0)

    # Assign color
    im_data = parula_map(Sx_norm, bytes=True)
    im_data_shape = im_data.shape

    im = Image.fromarray(im_data.astype(np.uint8), mode="RGBA")
    im = im.resize(
        (im_data_shape[1] * 5, im_data_shape[0]), resample=Image.Resampling.NEAREST
    )
    # im = im.convert("RGB")

    # add line to indicate subframe location
    draw = ImageDraw.Draw(im)
    subframe_width_pixel = int(im_data_shape[1] * 5 / (len(buf) / sf_size)) / 2
    for i in range(1, image_num_subframe + 1):
        draw.line(
            (i * subframe_width_pixel, 0, i * subframe_width_pixel, im_data_shape[0]),
            fill="red",
            width=1,
        )

    return im


async def receive_data(
    zmq_address: str,
    width: int,
    height: int,
    normalization: int = 10,
    ui_element: ui.row = UI_ROW_UPPER,
):
    context = zmq.asyncio.Context()
    socket_data = context.socket(zmq.SUB)
    socket_data.connect(zmq_address)
    socket_data.subscribe(b"")
    print(f"Connected to {zmq_address}")

    graph_name = zmq_address.split(".")[-1].replace("-", " ").upper() + " Graph"

    with ui_element:
        with ui.card():
            ui.chip(graph_name, color="primary", icon="content_paste_search").props(
                'outline size="1.5em"'
            ).classes("w-full").style("margin-top: -5px")

            graph_image = (
                ui.image()
                .style(f"width: {width}px; height: {height}px;")
                .props("no-transition")
            )
            with graph_image:
                waiting = (
                    ui.column()
                    .style("background-color: transparent; margin-left: 30px")
                    .classes("flex-center h-full")
                )
                with waiting:
                    ui.spinner(size="5em")
                    ui.chip("Waiting Signal...").classes("text-white")

    while True:
        data = await socket_data.recv()
        if len(data) == 0:
            continue

        if waiting is not None:
            waiting.delete()
            waiting = None

        img = calculate_spectogram(
            buf=data,
            sample_rate=23.04e6,
            fft_len=768,
            image_norm_factor=normalization,
        )
        img = img.resize((width, height), resample=Image.Resampling.NEAREST)
        graph_image.set_source(img)
        await asyncio.sleep(0.01)


async def update_status(zmq_address, ui_element=UI_LEFT_PANEL):
    with ui_element:
        with ui.card().style("margin-left: 25px; margin-right: 25px;"):
            ui.chip("5G NR Sniffer Status", color="primary", icon="wifi_find").props(
                'outline size="1.5em"'
            ).classes("w-full").style("margin-top: -5px")
            with ui.column().classes("gap-0"):

                ui.chip("Cell Status", icon="cell_tower").props(
                    "outline square dense"
                ).style("min-width: 290px")
                with ui.row(align_items="center"):
                    ui.html(
                        f'<lottie-player src="/static/cell-tower.json" loop autoplay />'
                    ).style("height: 70px;")
                    text_status_celltower = (
                        ui.label("Searching...").classes("text-lg").style("color: red;")
                    )

                ui.chip("UE Tracker", icon="smartphone").props(
                    "outline square dense"
                ).style("min-width: 290px")
                with ui.row(align_items="center"):
                    ui.html(
                        f'<lottie-player src="/static/ue-tracker.json" loop autoplay />'
                    ).style("height: 70px;")
                    text_status_ue = (
                        ui.label("Waiting...").classes("text-lg").style("color: red;")
                    )

                ui.chip("Carrier Frequency Offset (CFO)", icon="ssid_chart").props(
                    "outline square dense"
                ).style("min-width: 290px")
                text_cfo = ui.label("-- Hz").classes("text-lg w-full text-center")

                ui.chip(
                    "Reference Signal Received Power (RSRP)", icon="signal_cellular_alt"
                ).props("outline square dense").style("min-width: 290px")
                text_rsrp = ui.label("-- dB").classes("text-lg w-full text-center")
                graph_rsrp = (
                    ui.line_plot(limit=20, figsize=(3, 2), layout="compressed")
                    .classes("w-full")
                    .style("margin-top: -5px; margin-bottom: 10px;")
                )
                graph_rsrp.fig.gca().set_xticklabels([])
                graph_rsrp.fig.gca().grid(True)
                graph_rsrp.push([graph_rsrp.push_counter], [[0]], y_limits=(-100.0, 0))

                ui.chip(
                    "Signal-to-Noise Ratio (SNR)", icon="signal_cellular_alt"
                ).props("outline square dense").style("min-width: 290px")
                text_snr = ui.label("-- dB").classes("text-lg w-full text-center")
                graph_snr = (
                    ui.line_plot(limit=20, figsize=(3, 2), layout="compressed")
                    .classes("w-full")
                    .style("margin-top: -5px; margin-bottom: 10px;")
                )
                graph_snr.fig.gca().set_xticklabels([])
                graph_snr.fig.gca().grid(True)
                graph_snr.push([graph_rsrp.push_counter], [[0]], y_limits=(-100.0, 0))
    context = zmq.asyncio.Context()
    socket_data = context.socket(zmq.SUB)
    socket_data.connect(zmq_address)
    socket_data.subscribe(b"")
    print(f"Connected to {zmq_address}")

    while True:
        data = await socket_data.recv_json()
        if len(data) == 0:
            continue

        if "CELL" in data:
            cell = data["CELL"]
            tac = data["TAC"]
            mcc = data["MCC"]
            mnc = data["MNC"]
            text_status_celltower.clear()
            text_status_celltower.set_text(f"")
            text_status_celltower.style("color: green")
            with text_status_celltower:
                ui.label(f"CellID: {cell}").classes("text-md").style(
                    "margin-left: 15px;"
                )
                ui.chip(f"TAC: {tac}, MCC: {mcc}, MNC: {mnc}", color="green").style(
                    "font-weight: bold;"
                )
                ui.notify(
                    "Cell Tower Found",
                    title="Cell Status",
                    color="green",
                    timeout=1000,
                    type="positive",
                    position="top-left",
                )
            text_status_celltower.style("color: green;")

        if "UE" in data:
            ue = data["UE"]

            # Check if UE is disconnected
            if ue == False:
                text_status_ue.clear()
                text_status_ue.set_text("Waiting...")
                text_status_ue.style("color: red")
                ui.notify(
                    "Lost Track of UE!",
                    position="top-left",
                    timeout=1000,
                    type="negative",
                )
                continue

            text_status_ue.set_text(f"")
            text_status_ue.style("color: green")
            with text_status_ue:
                ui.chip(f"UE RNTI: {ue}").classes("text-md").style(
                    "font-weight: bold; min-width: 200px;"
                )
                ui.notify(
                    "UE Found",
                    title="UE Status",
                    color="green",
                    timeout=1000,
                    type="positive",
                    position="top-left",
                )
            text_status_ue.style("color: green;")

        if "CFO" in data:
            cfo = data["CFO"]
            text_cfo.set_text(f"{cfo} Hz")
        if "RSRP" in data:
            rsrp = data["RSRP"]
            text_rsrp.set_text(f"{rsrp} dB")
            graph_rsrp.push([graph_snr.push_counter], [[rsrp]], y_limits=(-100.0, 0))
        if "SNR" in data:
            snr = data["SNR"]
            text_snr.set_text(f"{snr} dB")
            graph_snr.push([graph_snr.push_counter], [[snr]], y_limits=(0, 40.0))


async def receive_logs(zmq_address, ui_element=UI_ROW_BOTTOM):
    global logger

    with ui_element:
        with ui.card().classes("w-full").style("max-width: 1112px;"):
            logger = Log(max_lines=10000).style(
                "height: 327px; background-color: #1e1e1e; color: white; font-weight: bold;"
            )

    context = zmq.asyncio.Context()
    socket_data = context.socket(zmq.SUB)
    socket_data.connect(zmq_address)
    socket_data.subscribe(b"")
    print(f"Connected to {zmq_address}")

    last_time = time.process_time()
    log_buffer = [""]
    packet_buffer = []
    log_timeout = 0.1

    async def logs_worker(log_buffer, timeout, packet_buffer):
        global sequence_diagram
        last_packet_buffer_len = 0

        while True:
            await asyncio.sleep(timeout)
            if len(log_buffer[0]):
                logger.push(conv.convert(log_buffer[0], full=False), True)
                log_buffer[0] = ""

            if len(packet_buffer) != last_packet_buffer_len:
                # Update mermaid sequence diagram
                mermaid_data = "sequenceDiagram\nparticipant gNB\nparticipant UE\n"
                for pkt in packet_buffer:
                    if "RRC" not in pkt and "NAS" not in pkt:
                        continue

                    direction = "gNB->>UE: " if "-->" in pkt else "UE->>gNB: "
                    pkt_info = pkt.split("[P:")[1].split("] ", 1)
                    proto = pkt_info[0].split("/")[-1]
                    summary = (
                        pkt_info[1]
                        .split("||   , ")[-1]
                        .split("[", 1)[0]
                        .split("(", 1)[0]
                    )

                    # Highlight relevant packets from UE
                    if "Identity response" in summary:
                        summary = f"{summary}\nNote over gNB: SUCI Identity Leaked!!!"

                    mermaid_data += f"    {direction}[{proto}] {summary}\n"

                last_packet_buffer_len = len(packet_buffer)
                sequence_diagram.set_content(mermaid_data)
                # sequence_diagram.run_method('scrollIntoView', 'false')

    asyncio.create_task(logs_worker(log_buffer, log_timeout, packet_buffer))

    while True:
        data = await socket_data.recv_string()
        if len(data) == 0:
            continue

        if time.process_time() - last_time < log_timeout:
            # Detect packet summary based on [P: tag]
            if "[P:" in data:
                if "RRC Setup Request" in data:
                    packet_buffer.clear()
                packet_buffer.append(data)

            log_buffer[0] += data[27:]
            continue

        logger.push(conv.convert(log_buffer[0], full=False), True)

        log_buffer[0] = ""
        last_time = time.process_time()


def show_file_dialog(file_path, language="C++"):
    with ui.dialog() as dialog, ui.card().classes("w-full").style(
        "max-width: 1200px; overflow: hidden;"
    ) as card:
        ui.chip(
            f"File Editor - {file_path}", color="primary", icon="description"
        ).props('outline size="1.5em"').classes("w-full").style("margin-top: -5px")
        with open(file_path, mode="r") as file:
            editor = ui.codemirror(
                value=file.read(), language=language, theme="vscodeDark"
            ).style("height: 800px;")

        with ui.row():
            ui.button("Close", color="red", icon="close", on_click=dialog.close).props(
                "push glossy"
            )
            if language == "C++":
                props = "push glossy disabled"
            else:
                props = "push glossy"
            ui.button(
                "Save",
                icon="save",
                color="secondary",
                on_click=lambda: (
                    open(file_path, "w").write(editor.value),
                    ui.notify(f"Saved: {file_path}", position="top-left"),
                ),
            ).props(props)

    return dialog.open()


def start_snif5get(config_file, module_file):
    global snif5gect_proc
    print(f"Config File: {config_file}")
    print(f"Module File: {module_file}")
    if check_snif5get_status():
        print("Sni5Gcet is already running!")
        ui.notify("Sni5Gcet is already running!", type="warning", position="top-left")
        return

    # Update temporary config file with selected module
    tmp_path = "/tmp/sni5gect.conf"
    copy(config_file, tmp_path)
    config.read(tmp_path)
    config["exploit"]["module"] = f"modules/lib_{module_file}.so"
    config.write(open(tmp_path, "w"))

    snif5gect_proc = subprocess.Popen(
        [f"sudo numactl -C !7 {SNI5GECT_BIN_PATH} {tmp_path}"],
        shell=True,
        stdin=None,
        stdout=None,
        stderr=None,
        close_fds=True,
        cwd=SNI5GECT_PATH,
    )


def stop_snif5get(force: bool = False):
    global snif5gect_proc
    if snif5gect_proc is not None or force:
        # kill by pid
        os.system(f'sudo pkill -SIGINT -f {SNI5GECT_BIN_PATH.split("/")[-1]}')
        snif5gect_proc = None
        print("Sni5Gcet Stopped!")
    else:
        print("Sni5Gcet is not running!")


def check_snif5get_status():
    global snif5gect_proc
    if snif5gect_proc is not None:
        if snif5gect_proc.poll() is None:
            return True
    return False


async def snif5get_controls(ui_element=UI_LEFT_PANEL):
    global logger

    with ui_element:
        with ui.card().style("margin-left: 25px; margin-right: 25px; width: 305px;"):
            ui.chip(
                "Snif5Gcet Controls", color="primary", icon="signal_cellular_alt"
            ).props('outline size="1.5em"').classes("w-full").style("margin-top: -5px")

            # Open config file
            sel_config_file = (
                ui.select(
                    config_files,
                    label="Select Config File",
                    value=config_files[default_config_idx],
                )
                .classes("w-full")
                .style("margin-top: -20px;")
            )
            ui.button(
                "Open Config File",
                icon="settings",
                color="secondary",
                on_click=lambda: show_file_dialog(
                    f"{SNI5GECT_CFG_PATH}/{sel_config_file.value}", "TTCN_CFG"
                ),
            ).classes("w-full").props("dense push glossy")
            # List Attack modes
            ui.chip("Attack Mode (C++ Module)", icon="extension").props(
                "square outline dense"
            ).classes("w-full")
            sel_module_file = (
                ui.select(
                    module_files,
                    label="Select Attack Script",
                    value=module_files[dummy_file_idx],
                )
                .classes("w-full")
                .style("margin-top: -20px;")
            )
            ui.button(
                "View Attack Script",
                icon="extension",
                color="secondary",
                on_click=lambda: show_file_dialog(
                    f"{SNI5GECT_MODULES_PATH}/{sel_module_file.value}.cc", "C++"
                ),
            ).classes("w-full").props("dense push glossy")

            btn_start = (
                ui.button(
                    "Start Sni5Gcet",
                    icon="play_arrow",
                    on_click=lambda: (
                        logger.clear(),
                        start_snif5get(
                            f"{SNI5GECT_CFG_PATH}/{sel_config_file.value}",
                            sel_module_file.value,
                        ),
                    ),
                )
                .classes("w-full")
                .props("dense push glossy")
            )
            btn_stop = (
                ui.button(
                    "Stop Sni5Gcet", icon="stop", color="red", on_click=stop_snif5get
                )
                .classes("w-full")
                .props("dense outline")
            )
            btn_stop.set_enabled(False)
            ui.button(
                "Clear Logs",
                icon="delete",
                color="red",
                on_click=lambda: logger.clear(),
            ).classes("w-full").props("dense push glossy")

            ui.chip("Process Status", icon="favorite").props(
                "square outline dense"
            ).classes("w-full")
            text_proc_status = (
                ui.label("Stopped")
                .classes("text-lg w-full text-center")
                .style("color: red; margin-top: -10px;")
            )

        last_proc_state = False
        while True:
            await asyncio.sleep(1)
            proc_state = check_snif5get_status()

            if last_proc_state == proc_state:
                continue

            if proc_state:
                text_proc_status.set_text("Running")
                text_proc_status.style("color: green;")
                btn_start.set_enabled(False)
                btn_stop.set_enabled(True)
                ui.notify(
                    "Sni5Gcet Started",
                    type="positive",
                    color="green",
                    position="top-left",
                    timeout=1000,
                )
            else:
                text_proc_status.set_text("Stopped")
                text_proc_status.style("color: red;")
                btn_start.set_enabled(True)
                btn_stop.set_enabled(False)
                ui.notify(
                    "Sni5Gcet Stopped",
                    type="warning",
                    position="top-left",
                    timeout=1000,
                )

            last_proc_state = proc_state


def enable_airplane_mode():
    os.system(CMD_ENABLE_AIRPLANE_MODE)


def disable_airplane_mode():
    os.system(CMD_DISABLE_AIRPLANE_MODE)


def check_ue_connection():
    try:
        output = (
            subprocess.check_output(CMD_NETWORK_STATUS, shell=True)
            .decode("utf-8")
            .strip("\r\n")
        )
        state = output.split("network=")[-1].split(",")[0]
    except:
        state = "UNKNOWN"
    return state


def check_ue_model():
    model_info = {}
    try:
        output = (
            subprocess.check_output(CMD_PHONE_GET_INFO, shell=True)
            .decode("utf-8")
            .split("\n")
        )
        output = [line.strip("\r") for line in output]

        for line in output:
            if "ro.product.vendor.manufacturer" in line:
                key_name = "manufacturer"
            elif "ro.product.vendor.model" in line:
                key_name = "model"
            elif "ro.soc.manufacturer" in line:
                key_name = "soc_manufacturer"
            elif "ro.soc.model" in line:
                key_name = "soc_model"
            elif "ro.serialno" in line:
                key_name = "serial"
            else:
                key_name = None

            if key_name is not None:
                model_info[key_name] = line.split("[")[-1].strip("]")

        pprint(model_info, indent=4)
    except Exception as e:
        pprint(e)

    return model_info


# Only update spinner visibility if the state changes
def update_spinner_visibility(
    spinner: ui.spinner, last_state: bool, target_state: bool
):
    if last_state != target_state:
        spinner.set_visibility(True)


async def ue_controls(ui_element=UI_LEFT_PANEL):
    status_spinner = None  # type: ui.spinner
    last_state = False

    with ui_element:
        with ui.card().style("margin-left: 25px; margin-right: 25px;"):
            ui.chip("UE Control (Via ADB)", color="primary", icon="smartphone").props(
                'outline size="1.5em"'
            ).classes("w-full").style("margin-top: -5px")
            ui.button(
                "Connect (Airplane Mode OFF)",
                icon="network_cell",
                on_click=lambda: (
                    disable_airplane_mode(),
                    update_spinner_visibility(status_spinner, last_state, "NR"),
                ),
            ).classes("w-full").props("dense push glossy")
            ui.button(
                "Disconnect (Airplane Mode ON)",
                icon="signal_cellular_nodata",
                color="red",
                on_click=lambda: (
                    enable_airplane_mode(),
                    update_spinner_visibility(status_spinner, last_state, "UNKNOWN"),
                ),
            ).classes("w-full").props("dense push glossy")
            ui.chip("Connectivity Status", icon="favorite").props(
                "square outline dense"
            ).classes("w-full")
            with ui.row().classes("text-lg w-full text-center"):
                with ui.list().props("dense separator").classes("w-full").style(
                    "margin-top: -20px"
                ) as list_ue_info:
                    ui.item("Model: ----")
                    ui.item("SoC: ----")
                text_ue_status = ui.label("Disconnected").style(
                    "color: red; margin-top: -14px; margin-left: 80px;"
                )
                status_spinner = ui.spinner("dots", size="2.5em").style(
                    "margin-bottom: -20px; margin-top: -23px;"
                )
                status_spinner.set_visibility(False)

    # Update model info (TODO: make this dynamic)
    model_info = check_ue_model()

    if len(model_info):
        list_ue_info.clear()
        with list_ue_info:
            ui.item(f'Model: {model_info["manufacturer"]} {model_info["model"]}')
            ui.item(f'SoC: {model_info["soc_manufacturer"]} {model_info["soc_model"]}')

    while True:
        await asyncio.sleep(1)

        state = check_ue_connection()

        if last_state == state:
            continue

        if state == "NR":
            text_ue_status.set_text("Connected")
            text_ue_status.style("color: green;")
        else:
            text_ue_status.set_text("Disconnected")
            text_ue_status.style("color: red;")

        last_state = state
        status_spinner.set_visibility(False)
        ui.notify("UE Connection Status Changed!", position="top-left", timeout=1000)


def show_sequence_diagram():
    global sequence_diagram
    with ui.card().style(
        "height: 864px; max-width: 800px; overflow-y: auto; overflow-x: hidden"
    ) as card:
        ui.chip(
            "Live Capture Summary (PCAP)", color="primary", icon="signal_cellular_alt"
        ).props('outline size="1.5em"').classes("w-full").style("margin-top: -5px")

        sequence_diagram = ui.mermaid(
            """                                     
                    """,
            config={
                "mirrorActors": False,
                "boxMargin": 2,
                "noteMargin": 5,
                "height": 20,
                "showSequenceNumbers": True,
            },
        ).style("width: 600px; overflow-y: auto;")

    return card

# Create UI layout
with ui.header(elevated=True):
    ui.markdown("## *Sni**5G**ect Dashboard*").style(
        "margin-bottom: -20px; margin-top: -30px;"
    )

with ui.column().classes("flex-center w-full"):
    with ui.row().classes("gap-5"):
        UI_LEFT_CTRL_PANEL = ui.column().classes("gap-5").style("max-width: 320px;")
        UI_LEFT_PANEL = ui.row().classes("gap-0")
        with ui.column(align_items="center"):
            UI_ROW_UPPER = ui.row().classes("gap-5 w-full")
            UI_ROW_BOTTOM = ui.row().classes("gap-5 w-full")
        UI_RIGHT_PANEL = show_sequence_diagram()

# Forcebly stop snif5get if running
stop_snif5get(force=True)
app.on_startup(ue_controls(UI_LEFT_CTRL_PANEL))
app.on_startup(snif5get_controls(UI_LEFT_CTRL_PANEL))
app.on_startup(update_status("ipc:///tmp/sni5gect", UI_LEFT_PANEL))
app.on_startup(receive_data("ipc:///tmp/sni5gect.dl-sib1", 200, 400, 10, UI_ROW_UPPER))
app.on_startup(receive_data("ipc:///tmp/sni5gect.dl-pdsch", 200, 400, 10, UI_ROW_UPPER))
app.on_startup(
    receive_data("ipc:///tmp/sni5gect.dl-dci-ul", 200, 400, 10, UI_ROW_UPPER)
)
app.on_startup(receive_data("ipc:///tmp/sni5gect.ul-pusch", 200, 400, 10, UI_ROW_UPPER))
app.on_startup(receive_logs("ipc:///tmp/sni5gect.logs", UI_ROW_BOTTOM))

# Add Lottie player
app.add_static_files("/static", "gui/static")
ui.add_css(open("gui/static/log-style.css").read())
ui.add_body_html(
    '<script src="https://unpkg.com/@lottiefiles/lottie-player@latest/dist/lottie-player.js"></script>'
)
ui.run(title="Sni5Gect Dashboard", favicon="gui/static/logo.png", reload=False, show=False)