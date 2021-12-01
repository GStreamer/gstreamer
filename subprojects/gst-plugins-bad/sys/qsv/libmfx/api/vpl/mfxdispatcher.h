/*############################################################################
  # Copyright Intel Corporation
  #
  # SPDX-License-Identifier: MIT
  ############################################################################*/

#ifndef __MFXDISPATCHER_H__
#define __MFXDISPATCHER_H__

#include "mfxdefs.h"
#include "mfxcommon.h"
#include "mfxsession.h"

#ifdef __cplusplus
extern "C" {
#endif

/*! Loader handle. */
typedef struct _mfxLoader *mfxLoader;

/*! Config handle. */
typedef struct _mfxConfig *mfxConfig;

/*!
   @brief Creates the loader.
   @return Loader Loader handle or NULL if failed.

   @since This function is available since API version 2.0.
*/
mfxLoader MFX_CDECL MFXLoad(void);

/*!
   @brief Destroys the dispatcher.
   @param[in] loader Loader handle.

   @since This function is available since API version 2.0.
*/
void MFX_CDECL MFXUnload(mfxLoader loader);

/*!
   @brief Creates dispatcher configuration.
   @details Creates the dispatcher internal configuration, which is used to filter out available implementations.
            This configuration is used to walk through selected implementations to gather more details and select the appropriate
            implementation to load. The loader object remembers all created mfxConfig objects and destroys them during the mfxUnload
            function call.

            Multiple configurations per single mfxLoader object are possible.

            Usage example:
            @code
               mfxLoader loader = MFXLoad();
               mfxConfig cfg = MFXCreateConfig(loader);
               MFXCreateSession(loader,0,&session);
            @endcode
   @param[in] loader Loader handle.
   @return Config handle or NULL pointer is failed.

   @since This function is available since API version 2.0.
*/
mfxConfig MFX_CDECL MFXCreateConfig(mfxLoader loader);

/*!
   @brief Adds additional filter properties (any fields of the mfxImplDescription structure) to the configuration of the loader object.
          One mfxConfig properties can hold only single filter property.
          @note Each new call with the same parameter name will overwrite the previously set value. This may invalidate other properties.
          @note Each new call with another parameter name will delete the previous property and create a new property based on new name's value.

   @param[in] config Config handle.
   @param[in] name Name of the parameter (see mfxImplDescription structure and example).
   @param[in] value Value of the parameter.
   @return
      MFX_ERR_NONE The function completed successfully.
      MFX_ERR_NULL_PTR    If config is NULL. \n
      MFX_ERR_NULL_PTR    If name is NULL. \n
      MFX_ERR_NOT_FOUND   If name contains unknown parameter name.
      MFX_ERR_UNSUPPORTED If value data type does not equal the parameter with provided name.

   @since This function is available since API version 2.0.
*/
mfxStatus MFX_CDECL MFXSetConfigFilterProperty(mfxConfig config, const mfxU8* name, mfxVariant value);

/*!
   @brief Iterates over filtered out implementations to gather their details. This function allocates memory to store
          mfxImplDescription structure instance. Use the MFXDispReleaseImplDescription function to free memory allocated to the mfxImplDescription structure.
   @param[in] loader Loader handle.
   @param[in] i Index of the implementation.
   @param[in] format Format in which capabilities need to be delivered. See the mfxImplCapsDeliveryFormat enumerator for more details.
   @param[out] idesc Pointer to the mfxImplDescription structure.
   @return
      MFX_ERR_NONE        The function completed successfully. The idesc contains valid information.\n
      MFX_ERR_NULL_PTR    If loader is NULL. \n
      MFX_ERR_NULL_PTR    If idesc is NULL. \n
      MFX_ERR_NOT_FOUND   Provided index is out of possible range. \n
      MFX_ERR_UNSUPPORTED If requested format is not supported.

   @since This function is available since API version 2.0.
*/
mfxStatus MFX_CDECL MFXEnumImplementations(mfxLoader loader, mfxU32 i, mfxImplCapsDeliveryFormat format, mfxHDL* idesc);


/*!
   @brief Loads and initializes the implementation.
   @code
      mfxLoader loader = MFXLoad();
      int i=0;
      while(1) {
         mfxImplDescription *idesc;
         MFXEnumImplementations(loader, i, MFX_IMPLCAPS_IMPLDESCSTRUCTURE, (mfxHDL*)&idesc);
         if(is_good(idesc)) {
             MFXCreateSession(loader, i,&session);
             // ...
             MFXDispReleaseImplDescription(loader, idesc);
         }
         else
         {
             MFXDispReleaseImplDescription(loader, idesc);
             break;
         }
      }
   @endcode
   @param[in] loader Loader handle.
   @param[in] i Index of the implementation.
   @param[out] session Pointer to the session handle.
   @return
      MFX_ERR_NONE        The function completed successfully. The session contains a pointer to the session handle.\n
      MFX_ERR_NULL_PTR    If loader is NULL. \n
      MFX_ERR_NULL_PTR    If session is NULL. \n
      MFX_ERR_NOT_FOUND   Provided index is out of possible range.

   @since This function is available since API version 2.0.
*/
mfxStatus MFX_CDECL MFXCreateSession(mfxLoader loader, mfxU32 i, mfxSession* session);

/*!
   @brief
      Destroys handle allocated by the MFXEnumImplementations function.

   @param[in] loader   Loader handle.
   @param[in] hdl      Handle to destroy. Can be equal to NULL.

   @return
      MFX_ERR_NONE           The function completed successfully. \n
      MFX_ERR_NULL_PTR       If loader is NULL. \n
      MFX_ERR_INVALID_HANDLE Provided hdl handle is not associated with this loader.

   @since This function is available since API version 2.0.
*/
mfxStatus MFX_CDECL MFXDispReleaseImplDescription(mfxLoader loader, mfxHDL hdl);

/* Helper macro definitions to add config filter properties. */

/*! Adds single property of mfxU32 type.
   @param[in] loader Valid mfxLoader object
   @param[in] name Property name string
   @param[in] value Property value
*/
#define MFX_ADD_PROPERTY_U32(loader, name, value)               \
{                                                               \
    mfxVariant impl_value;                                      \
    mfxConfig  cfg = MFXCreateConfig(loader);                   \
    impl_value.Version.Version = MFX_VARIANT_VERSION;           \
    impl_value.Type     = MFX_VARIANT_TYPE_U32;                 \
    impl_value.Data.U32 = value;                                \
    MFXSetConfigFilterProperty(cfg, (mfxU8 *)name, impl_value); \
}

/*! Adds single property of mfxU16 type.
   @param[in] loader Valid mfxLoader object
   @param[in] name Property name string
   @param[in] value Property value
*/
#define MFX_ADD_PROPERTY_U16(loader, name, value)               \
{                                                               \
    mfxVariant impl_value               = { 0 };                \
    mfxConfig  cfg = MFXCreateConfig(loader);                   \
    impl_value.Version.Version = MFX_VARIANT_VERSION;           \
    impl_value.Type     = MFX_VARIANT_TYPE_U16;                 \
    impl_value.Data.U16 = value;                                \
    MFXSetConfigFilterProperty(cfg, (mfxU8 *)name, impl_value); \
}

/*! Adds single property of pointer type.
   @param[in] loader Valid mfxLoader object
   @param[in] name Property name string
   @param[in] value Property value
*/
#define MFX_ADD_PROPERTY_PTR(loader, name, value)               \
{                                                               \
    mfxVariant impl_value               = { 0 };                \
    mfxConfig  cfg = MFXCreateConfig(loader);                   \
    impl_value.Version.Version = MFX_VARIANT_VERSION;           \
    impl_value.Type     = MFX_VARIANT_TYPE_PTR;                 \
    impl_value.Data.Ptr = (mfxHDL)value;                        \
    MFXSetConfigFilterProperty(cfg, (mfxU8 *)name, impl_value); \
}

/*! Update existing property of mfxU32 type.
   @param[in] loader Valid mfxLoader object
   @param[in] config Valid mfxConfig object
   @param[in] name Property name string
   @param[in] value Property value
*/
#define MFX_UPDATE_PROPERTY_U32(loader, config, name, value)       \
{                                                                  \
    mfxVariant impl_value;                                         \
    impl_value.Version.Version = MFX_VARIANT_VERSION;              \
    impl_value.Type     = MFX_VARIANT_TYPE_U32;                    \
    impl_value.Data.U32 = value;                                   \
    MFXSetConfigFilterProperty(config, (mfxU8 *)name, impl_value); \
}

/*! Update existing property of mfxU16 type.
   @param[in] loader Valid mfxLoader object
   @param[in] config Valid mfxConfig object
   @param[in] name Property name string
   @param[in] value Property value
*/
#define MFX_UPDATE_PROPERTY_U16(loader, config, name, value)       \
{                                                                  \
    mfxVariant impl_value;                                         \
    impl_value.Version.Version = MFX_VARIANT_VERSION;              \
    impl_value.Type     = MFX_VARIANT_TYPE_U16;                    \
    impl_value.Data.U16 = value;                                   \
    MFXSetConfigFilterProperty(config, (mfxU8 *)name, impl_value); \
}

/*! Update existing property of pointer type.
   @param[in] loader Valid mfxLoader object
   @param[in] config Valid mfxConfig object
   @param[in] name Property name string
   @param[in] value Property value
*/
#define MFX_UPDATE_PROPERTY_PTR(loader, config, name, value)       \
{                                                                  \
    mfxVariant impl_value;                                         \
    impl_value.Version.Version = MFX_VARIANT_VERSION;              \
    impl_value.Type     = MFX_VARIANT_TYPE_PTR;                    \
    impl_value.Data.Ptr = (mfxHDL)value;                           \
    MFXSetConfigFilterProperty(config, (mfxU8 *)name, impl_value); \
}

#ifdef __cplusplus
}
#endif

#endif

