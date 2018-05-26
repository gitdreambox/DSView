/*
 * This file is part of the DSView project.
 * DSView is based on PulseView.
 *
 * Copyright (C) 2013 DreamSourceLab <support@dreamsourcelab.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <extdef.h>
#include <assert.h>
#include <boost/foreach.hpp>

#include <QAction>
#include <QDebug>
#include <QLabel>
#include <QAbstractItemView>
#include <QApplication>

#include "samplingbar.h"

#include "../devicemanager.h"
#include "../device/devinst.h"
#include "../dialogs/deviceoptions.h"
#include "../dialogs/waitingdialog.h"
#include "../dialogs/dsmessagebox.h"
#include "../view/dsosignal.h"
#include "../dialogs/interval.h"

using namespace boost;
using boost::shared_ptr;
using std::map;
using std::max;
using std::min;
using std::string;

namespace pv {
namespace toolbars {

const QString SamplingBar::RLEString = tr("(RLE)");
const QString SamplingBar::DIVString = tr(" / div");

SamplingBar::SamplingBar(SigSession &session, QWidget *parent) :
	QToolBar("Sampling Bar", parent),
    _session(session),
    _enable(true),
    _sampling(false),
    _device_selector(this),
    _updating_device_selector(false),
    _configure_button(this),
    _sample_count(this),
    _sample_rate(this),
    _updating_sample_rate(false),
    _updating_sample_count(false),
    _icon_stop(":/icons/stop.png"),
    _icon_start(":/icons/start.png"),
    _icon_instant(":/icons/instant.png"),
    _icon_start_dis(":/icons/start_dis.png"),
    _icon_instant_dis(":/icons/instant_dis.png"),
    _run_stop_button(this),
    _instant_button(this),
    _mode_button(this),
    _icon_repeat(":/icons/moder.png"),
    _icon_single(":/icons/modes.png"),
    _icon_repeat_dis(":/icons/moder_dis.png"),
    _icon_single_dis(":/icons/modes_dis.png"),
    _instant(false)
{
    setMovable(false);
    layout()->setMargin(0);
    layout()->setSpacing(0);

    connect(&_device_selector, SIGNAL(currentIndexChanged (int)),
        this, SLOT(on_device_selected()));
    connect(&_configure_button, SIGNAL(clicked()),
        this, SLOT(on_configure()));
	connect(&_run_stop_button, SIGNAL(clicked()),
        this, SLOT(on_run_stop()), Qt::DirectConnection);
    connect(&_instant_button, SIGNAL(clicked()),
        this, SLOT(on_instant_stop()));

    _configure_button.setIcon(QIcon::fromTheme("configure",
        QIcon(":/icons/params.png")));

    _mode_button.setPopupMode(QToolButton::InstantPopup);
    _mode_button.setIcon(_session.get_run_mode() == pv::SigSession::Single ? _icon_single : _icon_repeat);
    _run_stop_button.setIcon(_icon_start);
    _instant_button.setIcon(_icon_instant);

    _device_selector.setSizeAdjustPolicy(QComboBox::AdjustToContents);
    _sample_rate.setSizeAdjustPolicy(QComboBox::AdjustToContents);
    _sample_count.setSizeAdjustPolicy(QComboBox::AdjustToContents);
    _device_selector.setMaximumWidth(ComboBoxMaxWidth);

	set_sampling(false);
    connect(&_sample_count, SIGNAL(currentIndexChanged(int)),
        this, SLOT(on_samplecount_sel(int)));

    //_run_stop_button.setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    _run_stop_button.setObjectName(tr("run_stop_button"));

    connect(&_sample_rate, SIGNAL(currentIndexChanged(int)),
        this, SLOT(on_samplerate_sel(int)));

    QWidget *leftMargin = new QWidget(this);
    leftMargin->setFixedWidth(4);
    addWidget(leftMargin);
    addWidget(&_device_selector);
    addWidget(&_configure_button);
    addWidget(&_sample_count);
    addWidget(new QLabel(tr(" @ ")));
    addWidget(&_sample_rate);

    _action_single = new QAction(this);
    _action_single->setText(QApplication::translate("Sampling", "&Single", 0));
    _action_single->setIcon(QIcon::fromTheme("Sampling",
        QIcon(":/icons/oneloop.png")));
    connect(_action_single, SIGNAL(triggered()), this, SLOT(on_mode()));

    _action_repeat = new QAction(this);
    _action_repeat->setText(QApplication::translate("Sampling", "&Repetitive", 0));
    _action_repeat->setIcon(QIcon::fromTheme("Sampling",
        QIcon(":/icons/repeat.png")));
    connect(_action_repeat, SIGNAL(triggered()), this, SLOT(on_mode()));

    _mode_menu = new QMenu(this);
    _mode_menu->addAction(_action_single);
    _mode_menu->addAction(_action_repeat);
    _mode_button.setMenu(_mode_menu);
    _mode_action = addWidget(&_mode_button);

    _run_stop_action = addWidget(&_run_stop_button);
    _instant_action = addWidget(&_instant_button);
}

void SamplingBar::set_device_list(
    const std::list< shared_ptr<pv::device::DevInst> > &devices,
    shared_ptr<pv::device::DevInst> selected)
{
    int selected_index = -1;

    assert(selected);

    _updating_device_selector = true;

    _device_selector.clear();
    _device_selector_map.clear();

    BOOST_FOREACH (shared_ptr<pv::device::DevInst> dev_inst, devices) {
        assert(dev_inst);
        const QString title = dev_inst->format_device_title();
        const void *id = dev_inst->get_id();
        assert(id);

        if (selected == dev_inst)
            selected_index = _device_selector.count();

        _device_selector_map[id] = dev_inst;
        _device_selector.addItem(title,
            qVariantFromValue((void*)id));
    }
    int width = _device_selector.sizeHint().width();
    _device_selector.setFixedWidth(min(width+15, _device_selector.maximumWidth()));
    _device_selector.view()->setMinimumWidth(width+30);

    // The selected device should have been in the list
    assert(selected_index != -1);
    _device_selector.setCurrentIndex(selected_index);

    update_sample_rate_selector();

    _updating_device_selector = false;
}

shared_ptr<pv::device::DevInst> SamplingBar::get_selected_device() const
{
    const int index = _device_selector.currentIndex();
    if (index < 0)
        return shared_ptr<pv::device::DevInst>();

    const void *const id =
        _device_selector.itemData(index).value<void*>();
    assert(id);

    map<const void*, boost::weak_ptr<device::DevInst> >::
        const_iterator iter = _device_selector_map.find(id);
    if (iter == _device_selector_map.end())
        return shared_ptr<pv::device::DevInst>();

    return shared_ptr<pv::device::DevInst>((*iter).second);
}

void SamplingBar::on_configure()
{
    hide_calibration();

    int  ret;
    shared_ptr<pv::device::DevInst> dev_inst = get_selected_device();
    assert(dev_inst);

    pv::dialogs::DeviceOptions dlg(this, dev_inst);
    ret = dlg.exec();
    if (ret == QDialog::Accepted) {
        device_updated();
        update_sample_rate_selector();

        GVariant* gvar;
        if (dev_inst->dev_inst()->mode == DSO) {
            gvar = dev_inst->get_config(NULL, NULL, SR_CONF_ZERO);
            if (gvar != NULL) {
                bool zero = g_variant_get_boolean(gvar);
                g_variant_unref(gvar);
                if (zero) {
                    zero_adj();
                    return;
                }
            }

            gvar = dev_inst->get_config(NULL, NULL, SR_CONF_CALI);
            if (gvar != NULL) {
                bool cali = g_variant_get_boolean(gvar);
                g_variant_unref(gvar);
                if (cali) {
                    show_calibration();
                    return;
                }
            }
        }
        gvar = dev_inst->get_config(NULL, NULL, SR_CONF_TEST);
        if (gvar != NULL) {
            bool test = g_variant_get_boolean(gvar);
            g_variant_unref(gvar);
            if (test) {
                update_sample_rate_selector_value();
                _sample_count.setDisabled(true);
                _sample_rate.setDisabled(true);
            } else {
                _sample_count.setDisabled(false);
                if (dev_inst->dev_inst()->mode != DSO)
                    _sample_rate.setDisabled(false);
            }
        }
    }
}

void SamplingBar::zero_adj()
{
    boost::shared_ptr<view::DsoSignal> dsoSig;
    BOOST_FOREACH(const boost::shared_ptr<view::Signal> s, _session.get_signals())
    {
        if (dsoSig = dynamic_pointer_cast<view::DsoSignal>(s))
            dsoSig->set_enable(true);
    }
    run_stop();

    pv::dialogs::WaitingDialog wait(this, get_selected_device());
    if (wait.start() ==QDialog::Rejected) {
        BOOST_FOREACH(const boost::shared_ptr<view::Signal> s, _session.get_signals())
        {
            if (dsoSig = dynamic_pointer_cast<view::DsoSignal>(s))
                dsoSig->commit_settings();
        }
    }

    if (_session.get_capture_state() == pv::SigSession::Running)
        on_run_stop();
}

bool SamplingBar::get_sampling() const
{
    return _sampling;
}

bool SamplingBar::get_instant() const
{
    return _instant;
}

void SamplingBar::set_sampling(bool sampling)
{
    lock_guard<boost::recursive_mutex> lock(_sampling_mutex);
    _sampling = sampling;
    if (_instant) {
        _instant_button.setIcon(sampling ? _icon_stop : _icon_instant);
        _run_stop_button.setIcon(sampling ? _icon_start_dis : _icon_start);
    } else {
        _run_stop_button.setIcon(sampling ? _icon_stop : _icon_start);
        _instant_button.setIcon(sampling ? _icon_instant_dis : _icon_instant);
    }

    if (!sampling) {
        enable_run_stop(true);
        enable_instant(true);
    } else {
        if (_instant)
            enable_instant(true);
        else
            enable_run_stop(true);
    }

    _mode_button.setEnabled(!sampling);
    _mode_button.setIcon(sampling ? (_session.get_run_mode() == pv::SigSession::Single ? _icon_single_dis : _icon_repeat_dis) :
                                    (_session.get_run_mode() == pv::SigSession::Single ? _icon_single : _icon_repeat));
    _configure_button.setEnabled(!sampling);
    _configure_button.setIcon(sampling ? QIcon(":/icons/params_dis.png") :
                                  QIcon(":/icons/params.png"));
}

void SamplingBar::set_sample_rate(uint64_t sample_rate)
{
    for (int i = _sample_rate.count() - 1; i >= 0; i--) {
        uint64_t cur_index_sample_rate = _sample_rate.itemData(
                    i).value<uint64_t>();
        if (sample_rate >= cur_index_sample_rate) {
            _sample_rate.setCurrentIndex(i);
            break;
        }
    }
    commit_settings();
}

void SamplingBar::update_sample_rate_selector()
{
	GVariant *gvar_dict, *gvar_list;
	const uint64_t *elements = NULL;
	gsize num_elements;

    if (_updating_sample_rate)
        return;

    disconnect(&_sample_rate, SIGNAL(currentIndexChanged(int)),
        this, SLOT(on_samplerate_sel(int)));
    const shared_ptr<device::DevInst> dev_inst = get_selected_device();
    if (!dev_inst)
        return;

    assert(!_updating_sample_rate);
    _updating_sample_rate = true;

    if (!(gvar_dict = dev_inst->list_config(NULL, SR_CONF_SAMPLERATE)))
    {
        _sample_rate.clear();
        _sample_rate.show();
        _updating_sample_rate = false;
        return;
    }

    if ((gvar_list = g_variant_lookup_value(gvar_dict,
			"samplerates", G_VARIANT_TYPE("at"))))
	{
		elements = (const uint64_t *)g_variant_get_fixed_array(
				gvar_list, &num_elements, sizeof(uint64_t));
        _sample_rate.clear();

		for (unsigned int i = 0; i < num_elements; i++)
		{
			char *const s = sr_samplerate_string(elements[i]);
            _sample_rate.addItem(QString(s),
				qVariantFromValue(elements[i]));
			g_free(s);
		}

        _sample_rate.show();
		g_variant_unref(gvar_list);
	}

    _sample_rate.setMinimumWidth(_sample_rate.sizeHint().width()+15);
    _sample_rate.view()->setMinimumWidth(_sample_rate.sizeHint().width()+30);

    _updating_sample_rate = false;
	g_variant_unref(gvar_dict);

    update_sample_rate_selector_value();
    connect(&_sample_rate, SIGNAL(currentIndexChanged(int)),
        this, SLOT(on_samplerate_sel(int)));

    update_sample_count_selector();
}

void SamplingBar::update_sample_rate_selector_value()
{
    if (_updating_sample_rate)
        return;

    const uint64_t samplerate = get_selected_device()->get_sample_rate();

    assert(!_updating_sample_rate);
    _updating_sample_rate = true;

    if (samplerate != _sample_rate.itemData(
                _sample_rate.currentIndex()).value<uint64_t>()) {
        for (int i = _sample_rate.count() - 1; i >= 0; i--) {
            if (samplerate >= _sample_rate.itemData(
                i).value<uint64_t>()) {
                _sample_rate.setCurrentIndex(i);
                break;
            }
        }
    }

    _updating_sample_rate = false;
}

void SamplingBar::on_samplerate_sel(int index)
{
    (void)index;
    const shared_ptr<device::DevInst> dev_inst = get_selected_device();
    if (dev_inst->dev_inst()->mode != DSO)
        update_sample_count_selector();
}

void SamplingBar::update_sample_count_selector()
{
    bool stream_mode = false;
    uint64_t hw_depth = 0;
    uint64_t sw_depth;
    double pre_duration = SR_SEC(1);
    double duration;

    if (_updating_sample_count)
        return;

    disconnect(&_sample_count, SIGNAL(currentIndexChanged(int)),
        this, SLOT(on_samplecount_sel(int)));

    assert(!_updating_sample_count);
    _updating_sample_count = true;

    const shared_ptr<device::DevInst> dev_inst = get_selected_device();
    GVariant* gvar = dev_inst->get_config(NULL, NULL, SR_CONF_STREAM);
    if (gvar != NULL) {
        stream_mode = g_variant_get_boolean(gvar);
        g_variant_unref(gvar);
    }
    gvar = dev_inst->get_config(NULL, NULL, SR_CONF_HW_DEPTH);
    if (gvar != NULL) {
        hw_depth = g_variant_get_uint64(gvar);
        g_variant_unref(gvar);
    }

    if (dev_inst->dev_inst()->mode == LOGIC) {
        #if defined(__x86_64__) || defined(_M_X64)
            sw_depth = LogicMaxSWDepth64;
        #elif defined(__i386) || defined(_M_IX86)
            int ch_num = _session.get_ch_num(SR_CHANNEL_LOGIC);
            if (ch_num <= 0)
                sw_depth = LogicMaxSWDepth32;
            else
                sw_depth = LogicMaxSWDepth32 / ch_num;
        #endif
    } else {
        sw_depth = AnalogMaxSWDepth;
    }

    if (0 != _sample_count.count())
        pre_duration = _sample_count.itemData(
                    _sample_count.currentIndex()).value<double>();
    _sample_count.clear();
    const uint64_t samplerate = _sample_rate.itemData(
                _sample_rate.currentIndex()).value<uint64_t>();
    if (dev_inst->dev_inst()->mode == DSO)
        duration = SR_SEC(10);
    else if (stream_mode)
        duration = sw_depth / (samplerate * (1.0 / SR_SEC(1)));
    else
        duration = hw_depth / (samplerate * (1.0 / SR_SEC(1)));

    bool not_last = true;
    do {
        char *const s = sr_time_string(duration);
        _sample_count.addItem(QString(s) + ((dev_inst->dev_inst()->mode == DSO) ? DIVString : ""),
            qVariantFromValue(duration));
        g_free(s);

        double unit;
        if (duration >= SR_DAY(1))
            unit = SR_DAY(1);
        else if (duration >= SR_HOUR(1))
            unit = SR_HOUR(1);
        else if (duration >= SR_MIN(1))
            unit = SR_MIN(1);
        else
            unit = 1;
        const double log10_duration = pow(10,
                                          floor(log10(duration / unit)));
        if (duration > 5 * log10_duration * unit)
            duration = 5 * log10_duration * unit;
        else if (duration > 2 * log10_duration * unit)
            duration = 2 * log10_duration * unit;
        else if (duration > log10_duration * unit)
            duration = log10_duration * unit;
        else
            duration = log10_duration > 1 ? duration * 0.5 :
                       (unit == SR_DAY(1) ? SR_HOUR(20) :
                        unit == SR_HOUR(1) ? SR_MIN(50) :
                        unit == SR_MIN(1) ? SR_SEC(50) : duration * 0.5);

        if (dev_inst->dev_inst()->mode == DSO)
            not_last = duration >= SR_NS(10);
        else
            not_last = (duration / SR_SEC(1) * samplerate >= SR_KB(1));
    } while(not_last);

    _updating_sample_count = true;
    if (pre_duration > _sample_count.itemData(0).value<double>())
        _sample_count.setCurrentIndex(0);
    else if (pre_duration < _sample_count.itemData(_sample_count.count()-1).value<double>())
        _sample_count.setCurrentIndex(_sample_count.count()-1);
    else {
        for (int i = 0; i < _sample_count.count(); i++)
            if (pre_duration >= _sample_count.itemData(
                i).value<double>()) {
                _sample_count.setCurrentIndex(i);
                break;
            }
        sample_count_changed();
    }
    _updating_sample_count = false;

    if (dev_inst->dev_inst()->mode == DSO)
        update_sample_count_selector_value();
    connect(&_sample_count, SIGNAL(currentIndexChanged(int)),
        this, SLOT(on_samplecount_sel(int)));
}

void SamplingBar::update_sample_count_selector_value()
{
    if (_updating_sample_count)
        return;

    GVariant* gvar;
    double hori_res;
    gvar = get_selected_device()->get_config(NULL, NULL, SR_CONF_TIMEBASE);
    if (gvar != NULL) {
        hori_res = g_variant_get_uint64(gvar);
        g_variant_unref(gvar);
    } else {
        qDebug() << "ERROR: config_get SR_CONF_TIMEBASE failed.";
        return;
    }
    assert(!_updating_sample_count);
    _updating_sample_count = true;

    if (hori_res != _sample_count.itemData(
                _sample_count.currentIndex()).value<double>()) {
        for (int i = _sample_count.count() - 1; i >= 0; i--) {
            if (hori_res == _sample_count.itemData(
                i).value<double>()) {
                _sample_count.setCurrentIndex(i);
                break;
            }
        }
    }

    _updating_sample_count = false;
}

void SamplingBar::on_samplecount_sel(int index)
{
    (void)index;

    const shared_ptr<device::DevInst> dev_inst = get_selected_device();
    if (dev_inst->dev_inst()->mode == DSO)
        commit_hori_res();
    else
        sample_count_changed();
}

double SamplingBar::get_hori_res()
{
    return _sample_count.itemData(_sample_count.currentIndex()).value<double>();
}

double SamplingBar::hori_knob(int dir)
{
    double hori_res = -1;
    if (0 == dir) {
        hori_res = commit_hori_res();
    } else if ((dir > 0) && (_sample_count.currentIndex() > 0)) {
        _sample_count.setCurrentIndex(_sample_count.currentIndex() - 1);
        hori_res = commit_hori_res();
    } else if ((dir < 0) && (_sample_count.currentIndex() < _sample_count.count() - 1)) {
        _sample_count.setCurrentIndex(_sample_count.currentIndex() + 1);
        hori_res = commit_hori_res();
    }
    return hori_res;
}

double SamplingBar::commit_hori_res()
{
    const double hori_res = _sample_count.itemData(
                           _sample_count.currentIndex()).value<double>();

    if (_session.get_capture_state() == SigSession::Running)
        _session.refresh(RefreshShort);
    const shared_ptr<device::DevInst> dev_inst = get_selected_device();
    const uint64_t sample_limit = dev_inst->get_sample_limit();
    GVariant* gvar;
    uint64_t max_sample_rate;
    gvar = dev_inst->get_config(NULL, NULL, SR_CONF_MAX_DSO_SAMPLERATE);
    if (gvar != NULL) {
        max_sample_rate = g_variant_get_uint64(gvar);
        g_variant_unref(gvar);
    } else {
        qDebug() << "ERROR: config_get SR_CONF_MAX_DSO_SAMPLERATE failed.";
        return -1;
    }

    const uint64_t sample_rate = min((uint64_t)(sample_limit * SR_SEC(1) /
                                                (hori_res * DS_CONF_DSO_HDIVS)),
                                     (uint64_t)(max_sample_rate /
                                                (_session.get_ch_num(DSO) ? _session.get_ch_num(DSO) : 1)));
    set_sample_rate(sample_rate);
    if (_session.get_capture_state() != SigSession::Stopped)
        _session.set_cur_samplerate(dev_inst->get_sample_rate());

    dev_inst->set_config(NULL, NULL, SR_CONF_TIMEBASE,
                         g_variant_new_uint64(hori_res));

    return hori_res;
}

void SamplingBar::commit_settings()
{
    const double sample_duration = _sample_count.itemData(
            _sample_count.currentIndex()).value<double>();
    const uint64_t sample_rate = _sample_rate.itemData(
            _sample_rate.currentIndex()).value<uint64_t>();
    const uint64_t sample_count = ceil(sample_duration / SR_SEC(1) *
                                       sample_rate);

    const shared_ptr<device::DevInst> dev_inst = get_selected_device();
    if (dev_inst) {
        if (sample_rate != dev_inst->get_sample_rate())
            dev_inst->set_config(NULL, NULL,
                                 SR_CONF_SAMPLERATE,
                                 g_variant_new_uint64(sample_rate));
        if (dev_inst->dev_inst()->mode != DSO) {
            if (sample_count != dev_inst->get_sample_limit())
                dev_inst->set_config(NULL, NULL,
                                     SR_CONF_LIMIT_SAMPLES,
                                     g_variant_new_uint64(sample_count));

            bool rle_mode = _sample_count.currentText().contains(RLEString);
            dev_inst->set_config(NULL, NULL,
                                 SR_CONF_RLE,
                                 g_variant_new_boolean(rle_mode));
        }
    }
}

void SamplingBar::on_run_stop()
{
    if (get_sampling() || _session.isRepeating()) {
        _session.set_repeating(false);
        bool wait_upload = false;
        if (_session.get_run_mode() != SigSession::Repetitive) {
            GVariant *gvar = get_selected_device()->get_config(NULL, NULL, SR_CONF_WAIT_UPLOAD);
            if (gvar != NULL) {
                wait_upload = g_variant_get_boolean(gvar);
                g_variant_unref(gvar);
            }
        }
        if (!wait_upload) {
            _session.stop_capture();
            _session.capture_state_changed(SigSession::Stopped);
        }
    } else {
        enable_run_stop(false);
        enable_instant(false);
        commit_settings();
        _instant = false;
        const shared_ptr<device::DevInst> dev_inst = get_selected_device();
        if (!dev_inst)
            return;
        if (dev_inst->dev_inst()->mode == DSO) {
            GVariant* gvar = dev_inst->get_config(NULL, NULL, SR_CONF_ZERO);
            if (gvar != NULL) {
                bool zero = g_variant_get_boolean(gvar);
                g_variant_unref(gvar);
                if (zero) {
                    dialogs::DSMessageBox msg(this);
                    msg.mBox()->setText(tr("Auto Calibration"));
                    msg.mBox()->setInformativeText(tr("Please adjust zero skew and save the result!"));
                    //msg.setStandardButtons(QMessageBox::Ok);
                    msg.mBox()->addButton(tr("Ok"), QMessageBox::AcceptRole);
                    msg.mBox()->addButton(tr("Skip"), QMessageBox::RejectRole);
                    msg.mBox()->setIcon(QMessageBox::Warning);
                    if (msg.exec()) {
                        zero_adj();
                    } else {
                        dev_inst->set_config(NULL, NULL, SR_CONF_ZERO, g_variant_new_boolean(false));
                        enable_run_stop(true);
                        enable_instant(true);
                    }
                    return;
                }
            }
        }
        run_stop();
    }
}

void SamplingBar::on_instant_stop()
{
    if (get_sampling()) {
        _session.set_repeating(false);
        bool wait_upload = false;
        if (_session.get_run_mode() != SigSession::Repetitive) {
            GVariant *gvar = get_selected_device()->get_config(NULL, NULL, SR_CONF_WAIT_UPLOAD);
            if (gvar != NULL) {
                wait_upload = g_variant_get_boolean(gvar);
                g_variant_unref(gvar);
            }
        }
        if (!wait_upload) {
            _session.stop_capture();
            _session.capture_state_changed(SigSession::Stopped);
        }
    } else {
        enable_run_stop(false);
        enable_instant(false);
        commit_settings();
        _instant = true;
        const shared_ptr<device::DevInst> dev_inst = get_selected_device();
        if (!dev_inst)
            return;
        if (dev_inst->dev_inst()->mode == DSO) {
            GVariant* gvar = dev_inst->get_config(NULL, NULL, SR_CONF_ZERO);
            if (gvar != NULL) {
                bool zero = g_variant_get_boolean(gvar);
                g_variant_unref(gvar);
                if (zero) {
                    dialogs::DSMessageBox msg(this);
                    msg.mBox()->setText(tr("Auto Calibration"));
                    msg.mBox()->setInformativeText(tr("Auto Calibration program will be started. Please keep all channels out of singal input. It can take a while!"));
                    //msg.mBox()->setStandardButtons(QMessageBox::Ok);
                    msg.mBox()->addButton(tr("Ok"), QMessageBox::AcceptRole);
                    msg.mBox()->addButton(tr("Skip"), QMessageBox::RejectRole);
                    msg.mBox()->setIcon(QMessageBox::Warning);
                    if (msg.exec()) {
                        zero_adj();
                    } else {
                        dev_inst->set_config(NULL, NULL, SR_CONF_ZERO, g_variant_new_boolean(false));
                        enable_run_stop(true);
                        enable_instant(true);
                    }
                    return;
                }
            }
        }
        instant_stop();
    }
}

void SamplingBar::on_device_selected()
{
    if (_updating_device_selector)
        return;

    _session.stop_capture();

    const shared_ptr<device::DevInst> dev_inst = get_selected_device();
    if (!dev_inst)
        return;

    try {
        _session.set_device(dev_inst);
    } catch(QString e) {
        show_session_error(tr("Failed to select ") + dev_inst->dev_inst()->model, e);
    }
    device_selected();
}

void SamplingBar::enable_toggle(bool enable)
{
    bool test = false;
    const shared_ptr<device::DevInst> dev_inst = get_selected_device();
    if (dev_inst && dev_inst->owner()) {
        GVariant *gvar = dev_inst->get_config(NULL, NULL, SR_CONF_TEST);
        if (gvar != NULL) {
            test = g_variant_get_boolean(gvar);
            g_variant_unref(gvar);
        }
    }
    if (!test) {
        _sample_count.setDisabled(!enable);
        if (dev_inst->dev_inst()->mode == DSO)
            _sample_rate.setDisabled(true);
        else
            _sample_rate.setDisabled(!enable);
    } else {
        _sample_count.setDisabled(true);
        _sample_rate.setDisabled(true);
    }
}

void SamplingBar::enable_run_stop(bool enable)
{
    _run_stop_button.setDisabled(!enable);
}

void SamplingBar::enable_instant(bool enable)
{
    _instant_button.setDisabled(!enable);
}

void SamplingBar::show_session_error(
    const QString text, const QString info_text)
{
    dialogs::DSMessageBox msg(this);
    msg.mBox()->setText(text);
    msg.mBox()->setInformativeText(info_text);
    msg.mBox()->setStandardButtons(QMessageBox::Ok);
    msg.mBox()->setIcon(QMessageBox::Warning);
    msg.exec();
}

void SamplingBar::reload()
{
    if (_session.get_device()->dev_inst()->mode == LOGIC) {
        _icon_instant = QIcon(":/icons/instant.png");
        _icon_instant_dis = QIcon(":/icons/instant_dis.png");
        _instant_button.setIcon(_icon_instant);
        if (_session.get_device()->name() == "virtual-session") {
            _mode_action->setVisible(false);
        } else {
            _mode_button.setIcon(_session.get_run_mode() == pv::SigSession::Single ? _icon_single : _icon_repeat);
            _mode_action->setVisible(true);
        }
        _run_stop_action->setVisible(true);
        _instant_action->setVisible(true);
        enable_toggle(true);
    } else if (_session.get_device()->dev_inst()->mode == ANALOG) {
        _mode_action->setVisible(false);
        _run_stop_action->setVisible(true);
        _instant_action->setVisible(false);
        enable_toggle(true);
    } else if (_session.get_device()->dev_inst()->mode == DSO) {
        _icon_instant = QIcon(":/icons/single.png");
        _icon_instant_dis = QIcon(":/icons/single_dis.png");
        _instant_button.setIcon(_icon_instant);
        _mode_action->setVisible(false);
        _run_stop_action->setVisible(true);
        _instant_action->setVisible(true);
        enable_toggle(true);
    }
    update();
}

void SamplingBar::on_mode()
{
    QAction *act = qobject_cast<QAction *>(sender());
    if (act == _action_single) {
        _mode_button.setIcon(_icon_single);
        _session.set_run_mode(pv::SigSession::Single);
    } else if (act == _action_repeat) {
        _mode_button.setIcon(_icon_repeat);
        pv::dialogs::Interval interval_dlg(_session, this);
        interval_dlg.exec();
        _session.set_run_mode(pv::SigSession::Repetitive);
    }
}

} // namespace toolbars
} // namespace pv
