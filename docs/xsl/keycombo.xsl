<?xml version='1.0'?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:fo="http://www.w3.org/1999/XSL/Format"
                version='1.0'>

<!-- There is a bug in docbook-xsl-1.45; work around it here. -->
<!-- Also change it slightly for emacs key descriptions.      -->
<xsl:template match="keycombo">
 <xsl:variable name="action" select="@action"/>
 <xsl:variable name="joinchar">
  <xsl:choose>
   <xsl:when test="$action='seq'"><xsl:text> </xsl:text></xsl:when>
   <xsl:when test="$action='simul'">-</xsl:when>
   <xsl:otherwise>-</xsl:otherwise>
  </xsl:choose>
 </xsl:variable>
 <xsl:for-each select="./*">
  <xsl:if test="position()>1">
   <xsl:value-of select="$joinchar"/>
  </xsl:if>
  <xsl:apply-templates select="."/>
 </xsl:for-each>
</xsl:template>

</xsl:stylesheet>

