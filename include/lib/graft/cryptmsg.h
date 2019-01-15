#include "crypto/crypto.h"

namespace graft::cryptmsg {

/*!
 * \brief encryptMessage - encrypts data for recipients using their B public keys (assumed public view keys).
 *
 * \param data - data to encrypt.
 * \param Bkeys - pointer to array of B keys for each recipient.
 * \param BkeysCount - count of recipients.
 * \param message - resulting encripted message.
 */
void encryptMessage(const std::string data, const crypto::public_key* Bkeys, size_t BkeysCount, std::string& message);

/*!
 * \brief decryptMessage - (reverse of encryptForBs) decrypts data for one of the recipients using his b secret key.
 *
 * \param message - data that was created by encryptForBs.
 * \param bkey - secret key corresponding to one of Bs that were used to encrypt.
 * \param data - resulting decrypted data.
 * \return true on success or false otherwise
 */
bool decryptMessage(const std::string message, const crypto::secret_key& bkey, std::string& data);

} //namespace graft::cryptmsg
