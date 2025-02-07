/******************************************************/
/*                                                    */
/* configdialog.h - configuration dialog              */
/*                                                    */
/******************************************************/
/* Copyright 2019 Pierre Abbat.
 * This file is part of PerfectTIN.
 *
 * PerfectTIN is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * PerfectTIN is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License and Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * and Lesser General Public License along with PerfectTIN. If not, see
 * <http://www.gnu.org/licenses/>.
 */

#ifndef CONFIGDIALOG_H
#define CONFIGDIALOG_H
#include <vector>
#include <QDialog>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLineEdit>
#include <QGridLayout>

extern const double conversionFactors[4];
extern const char unitNames[4][12];

class ConfigurationDialog: public QDialog
{
  Q_OBJECT
public:
  ConfigurationDialog(QWidget *parent=0);
signals:
  void settingsChanged(double lu,double tol,int thr);
public slots:
  void set(double lengthUnit,double tolerance,int threads);
  void updateToleranceConversion();
  virtual void accept();
private:
  QLabel *lengthUnitLabel,*toleranceLabel;
  QLabel *threadLabel,*threadDefault;
  QLabel *toleranceInUnit;
  QComboBox *lengthUnitBox,*toleranceBox;
  QPushButton *okButton,*cancelButton;
  QGridLayout *gridLayout;
  QLineEdit *threadInput;
};
#endif

