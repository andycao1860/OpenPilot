/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef PFDQMLGADGETOPTIONSPAGE_H
#define PFDQMLGADGETOPTIONSPAGE_H

#include "coreplugin/dialogs/ioptionspage.h"
#include "QString"
#include <QStringList>
#include <QDebug>

namespace Core {
class IUAVGadgetConfiguration;
}

class PfdQmlGadgetConfiguration;

namespace Ui {
class PfdQmlGadgetOptionsPage;
}

using namespace Core;

class PfdQmlGadgetOptionsPage : public IOptionsPage {
    Q_OBJECT
public:
    explicit PfdQmlGadgetOptionsPage(PfdQmlGadgetConfiguration *config, QObject *parent = 0);

    QWidget *createPage(QWidget *parent);
    void apply();
    void finish();

private:
    Ui::PfdQmlGadgetOptionsPage *options_page;
    PfdQmlGadgetConfiguration *m_config;

private slots:
};

#endif // PfdQmlGADGETOPTIONSPAGE_H
