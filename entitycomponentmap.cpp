#include "entitycomponentmap.h"

#include <ve_commandevent.h>
#include <vcmp_componentdata.h>

#include <QString>

using namespace VeinEvent;
using namespace VeinComponent;

Q_LOGGING_CATEGORY(VEIN_API_QML_INTROSPECTION, "\e[1;37m<Vein.Api.QML.Introspection>\033[0m")

namespace VeinApiQml
{
  EntityComponentMap::EntityComponentMap(int t_entityId, const QJsonObject &t_entityIntrospection, QObject *t_parent) :
    QQmlPropertyMap(this, t_parent),
    m_entityIntrospection(t_entityIntrospection),
    m_entityId(t_entityId)
  {
  }

  void EntityComponentMap::processComponentData(VeinComponent::ComponentData *t_cData)
  {
    switch(t_cData->eventCommand())
    {
      case VeinComponent::ComponentData::Command::CCMD_ADD:
      case VeinComponent::ComponentData::Command::CCMD_SET:
      {
        qCDebug(VEIN_API_QML_VERBOSE) << "Updated value" << t_cData->componentName() << t_cData->newValue();
        insert(t_cData->componentName(), t_cData->newValue());
        break;
      }
      case VeinComponent::ComponentData::Command::CCMD_REMOVE:
      {
        clear(t_cData->componentName());
        break;
      }
      case VeinComponent::ComponentData::Command::CCMD_FETCH:
      {
        if(m_pendingValues.contains(t_cData->componentName()))
        {
          qCDebug(VEIN_API_QML_VERBOSE) << "Fetched value" << t_cData->componentName() << t_cData->newValue();
          insert(t_cData->componentName(), t_cData->newValue());
          m_pendingValues.removeAll(t_cData->componentName());
          if(m_pendingValues.isEmpty())
          {
            emit sigLoadedChanged(m_entityId);
          }
        }
        break;
      }
      default:
      {
        break;
      }
    }
  }

  EntityComponentMap::DataState EntityComponentMap::state() const
  {
    return m_state;
  }

  void EntityComponentMap::setState(EntityComponentMap::DataState t_dataState)
  {
    m_state = t_dataState;
    if(m_state == DataState::ECM_PENDING)
    {
      loadEntityData();
    }
    emit sigStateChanged(m_state);
  }

  int EntityComponentMap::entityId() const
  {
    return m_entityId;
  }

  QVariant EntityComponentMap::updateValue(const QString &t_key, const QVariant &t_newValue)
  {
    QVariant retVal = value(t_key);
    if(retVal != t_newValue)
    {
      CommandEvent *cEvent = 0;
      ComponentData *cData = 0;
      cData = new VeinComponent::ComponentData();
      cData->setEntityId(m_entityId);
      cData->setCommand(VeinComponent::ComponentData::Command::CCMD_SET);
      cData->setEventOrigin(VeinComponent::ComponentData::EventOrigin::EO_LOCAL);
      cData->setEventTarget(VeinComponent::ComponentData::EventTarget::ET_ALL);
      cData->setComponentName(t_key);
      cData->setNewValue(t_newValue);
      cData->setOldValue(retVal);
      cEvent = new CommandEvent(CommandEvent::EventSubtype::TRANSACTION, cData);

      emit sigSendEvent(cEvent);
    }
    return retVal;
  }

  void EntityComponentMap::loadEntityData()
  {
    CommandEvent *cEvent = 0;
    ComponentData *cData = 0;

    QVariantMap tmpValues = m_entityIntrospection.toVariantMap();
    QList<QString> tmpKeys = tmpValues.value(QString("components")).toStringList();
    m_pendingValues.append(tmpKeys);
    foreach(QString tmpKey, tmpKeys)
    {
      insert(tmpKey, QVariant());

      cData = new VeinComponent::ComponentData();
      cData->setEntityId(m_entityId);
      cData->setCommand(VeinComponent::ComponentData::Command::CCMD_FETCH);
      cData->setEventOrigin(VeinComponent::ComponentData::EventOrigin::EO_LOCAL);
      cData->setEventTarget(VeinComponent::ComponentData::EventTarget::ET_ALL);
      cData->setComponentName(tmpKey);
      cEvent = new CommandEvent(CommandEvent::EventSubtype::TRANSACTION, cData);
      qCDebug(VEIN_API_QML_INTROSPECTION) << "Fetching entity data for entityId:" << m_entityId << "component:" << tmpKey << "event:" << cEvent;

      emit sigSendEvent(cEvent);
    }
  }
}
