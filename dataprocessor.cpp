/*
 *    This application is able to read the IMU 9-DOF sensor stick
 *    from Sparfunk SEN-10724.
 *
 *    Copyright (C) 2013 Simon Stürz (stuerz.simon@gmail.com)
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <QJsonDocument>
#include <QVariantMap>
#include <QVector3D>
#include <QMatrix>
#include <QMatrix3x3>
#include <QDebug>
#include <QSettings>
#include <math.h>

#include "dataprocessor.h"

#define gravity 256

DataProcessor::DataProcessor(QObject *parent) :
    QObject(parent)
{
    m_gyrXangle = 0;
    m_gyrYangle = 0;
    m_dcmFilter = new DcmFilter(this);

    loadCalibrationParameters();
}

// this method gets the rawdata from the sensor, compensates sensorerrors, filter the data and calculates the angles...
void DataProcessor::processData(const QVector3D &accData, const QVector3D &gyroData, const QVector3D &magData, const int &dt)
{
    // ==============================================================================
    // first need to compensate the sensorerrors by using the calibrationdata...
    calibrateData(accData,gyroData,magData,dt);
    emit calibratedDataReady(m_acc,m_gyr,m_mag,dt);


    // ==============================================================================
    // now we can calculate the roll pitch angles...
    QVector3D lastAngles = m_angles;
    m_angles = m_dcmFilter->updateData(m_acc,m_gyr,m_mag,dt);

    //calculate velocity
    QVector3D anglesVel = (m_angles - lastAngles) / dt;

    emit anglesReady(m_angles,anglesVel);

    // ==============================================================================
    // send all data in JSON format to TCp Server
    serializeAllData(m_acc,m_gyr,m_mag,m_angles * 180 / M_PI,dt);

}


bool DataProcessor::loadCalibrationParameters()
{
    // TODO: check if data is ok  .toInt(&ok); when not return false and exit(1)

    // load all calibration parameters...
    QSettings settings("imu-utils");
    settings.beginGroup("Acc_calibration");
    int acc_x_max = settings.value("acc_x_max",999).toInt();
    int acc_x_min = settings.value("acc_x_min",999).toInt();
    int acc_y_max = settings.value("acc_y_max",999).toInt();
    int acc_y_min = settings.value("acc_y_min",999).toInt();
    int acc_z_max = settings.value("acc_z_max",999).toInt();
    int acc_z_min = settings.value("acc_z_min",999).toInt();
    settings.endGroup();

    settings.beginGroup("Gyr_calibration");
    gyr_x_offset = settings.value("gyr_x_offset",999).toFloat();
    gyr_y_offset = settings.value("gyr_y_offset",999).toFloat();
    gyr_z_offset = settings.value("gyr_z_offset",999).toFloat();
    settings.endGroup();

    settings.beginGroup("Mag_calibration");
    int mag_x_max = settings.value("mag_x_max",999).toInt();
    int mag_x_min = settings.value("mag_x_min",999).toInt();
    int mag_y_max = settings.value("mag_y_max",999).toInt();
    int mag_y_min = settings.value("mag_y_min",999).toInt();
    int mag_z_max = settings.value("mag_z_max",999).toInt();
    int mag_z_min = settings.value("mag_z_min",999).toInt();
    settings.endGroup();
    qDebug() << "--------------------------------------------";
    qDebug() << "-> calibration data loaded from" << settings.fileName();
    qDebug() << "   Acc  (min/max):     X  =" << acc_x_min << "/" << acc_x_max << "\tY =" << acc_y_min << "/" << acc_y_max << "\tZ =" << acc_z_min << "/" << acc_z_max ;
    qDebug() << "   Mag  (min/max):     X  =" << mag_x_min << "/" << mag_x_max << "\tY =" << mag_y_min << "/" << mag_y_max << "\tZ =" << mag_z_min << "/" << mag_z_max ;
    qDebug() << "   Gyro (offset) :     X  =" << gyr_x_offset << "\tY =" << gyr_y_offset << "\tZ =" << gyr_z_offset;

    // calculate acc offsets and scale factor
    acc_x_offset = (float)(acc_x_min + acc_x_max)/2;
    acc_y_offset = (float)(acc_y_min + acc_y_max)/2;
    acc_z_offset = (float)(acc_z_min + acc_z_max)/2;
    acc_x_scale  = gravity / (acc_x_max-acc_x_offset);
    acc_y_scale  = gravity / (acc_y_max-acc_y_offset);
    acc_z_scale  = gravity / (acc_z_max-acc_z_offset);

    // calculate mag offsets and scale factor
    mag_x_offset = (float)(mag_x_min + mag_x_max)/2;
    mag_y_offset = (float)(mag_y_min + mag_y_max)/2;
    mag_z_offset = (float)(mag_z_min + mag_z_max)/2;
    mag_x_scale  = (float)100 / (mag_x_max-mag_x_offset);
    mag_y_scale  = (float)100 / (mag_y_max-mag_y_offset);
    mag_z_scale  = (float)100 / (mag_z_max-mag_z_offset);

    // TODO: evaluate the calibration: is it good or bad...needs a new calibration?
    return true;
}

void DataProcessor::calibrateData(const QVector3D &accData, const QVector3D &gyroData, const QVector3D &magData, const int &dt)
{
    // compensating  the data using the values from
    QVector3D accVector;
    QVector3D magVector;
    QVector3D gyrVector;
    // compensate the acceleration vector (so the vector has the same length in both directions)
    accVector.setX((accData.x() - acc_x_offset) - acc_x_scale);
    accVector.setY((accData.y() - acc_y_offset) - acc_y_scale);
    accVector.setZ((accData.z() - acc_z_offset) - acc_z_scale);

    // compensate the magnetometer vector (so the vector has the same length in both directions)
    magVector.setX(((magData.x() - mag_x_offset) - mag_x_scale));
    magVector.setY(((magData.y() - mag_y_offset) - mag_y_scale));
    magVector.setZ(((magData.z() - mag_z_offset) - mag_z_scale));

    // compensate the gyroscop vector (bring it to 0 when not moving)
    gyrVector.setX(gyroData.x() - gyr_x_offset);
    gyrVector.setY(gyroData.y() - gyr_y_offset);
    gyrVector.setZ(gyroData.z() - gyr_z_offset);

    // save normalized acc - mag vector, and the zeroed gyr vector and the dt fpr this data
    m_acc = accVector;
    m_gyr = gyrVector;
    m_mag = magVector;
    m_dt = dt;

}


float DataProcessor::toDeg(float rad)
{
    return rad * 180 / M_PI;
}

float DataProcessor::toRad(float deg)
{
    return deg * M_PI /180;
}

void DataProcessor::serializeAllData(const QVector3D &accData, const QVector3D &gyroData, const QVector3D &magData, const QVector3D &angles, const int &dt)
{
    QVariantMap message;

    QVariantMap accMap;
    accMap.insert("x",accData.x());
    accMap.insert("y",accData.y());
    accMap.insert("z",accData.z());

    QVariantMap gyroMap;
    gyroMap.insert("x",gyroData.x());
    gyroMap.insert("y",gyroData.y());
    gyroMap.insert("z",gyroData.z());

    QVariantMap magMap;
    magMap.insert("x",magData.x());
    magMap.insert("y",magData.y());
    magMap.insert("z",magData.z());

    QVariantMap angleMap;
    angleMap.insert("roll",angles.x());
    angleMap.insert("pitch",angles.y());
    angleMap.insert("yaw",angles.z());

    message.insert("acc",accMap);
    message.insert("gyr",gyroMap);
    message.insert("mag",magMap);
    message.insert("angles",angleMap);
    message.insert("dt",dt);

    QByteArray data = QJsonDocument::fromVariant(message).toJson();
    data.append("\n");

    emit dataTcpReady(data);
}
